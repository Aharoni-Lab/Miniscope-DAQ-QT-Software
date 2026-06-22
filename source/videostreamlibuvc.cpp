#include "videostreamlibuvc.h"

#ifdef HAVE_LIBUVC

#include <QDebug>
#include <QCoreApplication>
#include <QDateTime>
#include <QThread>
#include <QtMath>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <climits>
#include <cstdlib>
#include <unistd.h>

// The Miniscope DAQ presents as a Cypress FX3 UVC device.
static const int MINISCOPE_VID = 0x04b4;
static const int MINISCOPE_PID = 0x00f9;

// Inter-command settle time. The DAQ control endpoint is slow to clear; on
// Linux (faster USB than Windows) we must wait or a command gets overwritten.
// Mirrors the >100us wait in VideoStreamOCV::camSetProperty.
static const useconds_t CTRL_SETTLE_US = 200;

VideoStreamLibUVC::VideoStreamLibUVC(QObject *parent, int width, int height, double pixelClock) :
    VideoStreamBase(parent),
    m_cameraID(-1),
    m_deviceName(""),
    m_ctx(nullptr),
    m_dev(nullptr),
    m_devh(nullptr),
    m_strmh(nullptr),
    m_negotiatedFps(0),
    m_isStreaming(false),
    m_stopStreaming(false),
    m_headOrientationStreamState(false),
    m_headOrientationFilterState(false),
    m_isColor(false),
    frameBuffer(nullptr),
    timeStampBuffer(nullptr),
    bnoBuffer(nullptr),
    freeFrames(nullptr),
    usedFrames(nullptr),
    frameBufferSize(0),
    m_acqFrameNum(nullptr),
    daqFrameNum(nullptr),
    m_trackExtTrigger(false),
    m_expectedWidth(width > 0 ? width : 608),
    m_expectedHeight(height > 0 ? height : 608),
    m_pixelClock(pixelClock),
    m_connectionType("")
{
}

VideoStreamLibUVC::~VideoStreamLibUVC()
{
    qDebug() << "Closing libuvc video stream";
    closeStream();
    closeDevice();
}

void VideoStreamLibUVC::closeStream()
{
    if (m_strmh) {
        uvc_stream_close(m_strmh); // stops streaming if needed
        m_strmh = nullptr;
    }
}

void VideoStreamLibUVC::closeDevice()
{
    if (m_devh) { uvc_close(m_devh); m_devh = nullptr; } // re-attaches kernel driver
    if (m_dev)  { uvc_unref_device(m_dev); m_dev = nullptr; }
    if (m_ctx)  { uvc_exit(m_ctx); m_ctx = nullptr; }
}

void VideoStreamLibUVC::setBufferParameters(cv::Mat *frameBuf, qint64 *tsBuf, float *bnoBuf,
                                            int bufferSize, QSemaphore *freeFramesS, QSemaphore *usedFramesS,
                                            QAtomicInt *acqFrameNum, QAtomicInt *daqFrameNumber)
{
    frameBuffer = frameBuf;
    timeStampBuffer = tsBuf;
    bnoBuffer = bnoBuf;
    frameBufferSize = bufferSize;
    freeFrames = freeFramesS;
    usedFrames = usedFramesS;
    m_acqFrameNum = acqFrameNum;
    daqFrameNum = daqFrameNumber;
}

// Resolve /dev/video{cameraID} to its USB bus/address by walking sysfs, so we
// open exactly the device the user selected (robust with multiple Miniscopes).
static bool videoIndexToBusAddr(int cameraID, int *busOut, int *addrOut)
{
    char linkPath[PATH_MAX];
    snprintf(linkPath, sizeof(linkPath), "/sys/class/video4linux/video%d/device", cameraID);
    char real[PATH_MAX];
    if (!realpath(linkPath, real))
        return false;

    // Walk up directories until we find busnum/devnum (the USB device node).
    QString dir = QString::fromLocal8Bit(real);
    for (int up = 0; up < 6 && !dir.isEmpty() && dir != "/"; up++) {
        FILE *fb = fopen((dir + "/busnum").toLocal8Bit().constData(), "r");
        FILE *fd = fopen((dir + "/devnum").toLocal8Bit().constData(), "r");
        if (fb && fd) {
            int b = -1, d = -1;
            if (fscanf(fb, "%d", &b) == 1 && fscanf(fd, "%d", &d) == 1) {
                fclose(fb); fclose(fd);
                *busOut = b; *addrOut = d;
                return true;
            }
        }
        if (fb) fclose(fb);
        if (fd) fclose(fd);
        int slash = dir.lastIndexOf('/');
        if (slash <= 0) break;
        dir = dir.left(slash);
    }
    return false;
}

bool VideoStreamLibUVC::openByVideoIndex(int cameraID)
{
    if (uvc_init(&m_ctx, nullptr) < 0) {
        sendMessage("Error: libuvc init failed for " + m_deviceName);
        m_ctx = nullptr;
        return false;
    }

    int wantBus = -1, wantAddr = -1;
    bool haveBusAddr = videoIndexToBusAddr(cameraID, &wantBus, &wantAddr);

    if (haveBusAddr) {
        // Match the specific USB device backing /dev/video{cameraID}.
        uvc_device_t **list = nullptr;
        if (uvc_get_device_list(m_ctx, &list) == UVC_SUCCESS && list) {
            for (int i = 0; list[i] != nullptr; i++) {
                if (uvc_get_bus_number(list[i]) == wantBus &&
                    uvc_get_device_address(list[i]) == wantAddr) {
                    m_dev = list[i];
                    uvc_ref_device(m_dev);
                    break;
                }
            }
            uvc_free_device_list(list, 1);
        }
    }

    // Fallback: first device matching the Miniscope VID/PID.
    if (!m_dev) {
        if (uvc_find_device(m_ctx, &m_dev, MINISCOPE_VID, MINISCOPE_PID, nullptr) < 0)
            m_dev = nullptr;
    }

    if (!m_dev) {
        sendMessage("Error: could not find Miniscope USB device for " + m_deviceName);
        return false;
    }

    if (uvc_open(m_dev, &m_devh) < 0) {
        sendMessage("Error: could not open " + m_deviceName + " via libuvc (device busy?)");
        m_devh = nullptr;
        return false;
    }
    return true;
}

bool VideoStreamLibUVC::negotiateFormat()
{
    const int fpsCandidates[] = {30, 20, 15, 10, 5, 60, 25};
    for (int fps : fpsCandidates) {
        if (uvc_get_stream_ctrl_format_size(m_devh, &m_streamCtrl, UVC_FRAME_FORMAT_YUYV,
                                            m_expectedWidth, m_expectedHeight, fps) == UVC_SUCCESS) {
            m_negotiatedFps = fps;
            return true;
        }
    }
    return false;
}

void VideoStreamLibUVC::sendSerdesModeCommands()
{
    // Mirror VideoStreamOCV::connect2Camera SERDES setup (TI 913/914).
    QVector<quint8> packet;
    if (m_pixelClock > 0) {
        if (m_pixelClock <= 50) {
            packet = {0xC0, 0x1F, 0b00010000}; setPropertyI2C(0, packet); // DES, 12bit low
            packet = {0xB0, 0x05, 0b00100000}; setPropertyI2C(1, packet); // SER
        } else {
            packet = {0xC0, 0x1F, 0b00010001}; setPropertyI2C(0, packet); // DES, 10bit high
            packet = {0xB0, 0x05, 0b00100001}; setPropertyI2C(1, packet); // SER
        }
        sendCommands();
        QThread::msleep(500);
    }
}

int VideoStreamLibUVC::connect2Camera(int cameraID)
{
    m_cameraID = cameraID;
    if (!openByVideoIndex(cameraID)) {
        closeDevice();
        return 0;
    }
    if (!negotiateFormat()) {
        sendMessage("Error: could not negotiate YUYV " + QString::number(m_expectedWidth) + "x" +
                    QString::number(m_expectedHeight) + " for " + m_deviceName);
        closeDevice();
        return 0;
    }
    m_connectionType = "libuvc";
    sendSerdesModeCommands();
    return 1;
}

int VideoStreamLibUVC::connect2Video(QString, QString, float)
{
    // Video-file playback always uses the OpenCV backend; not supported here.
    sendMessage("Error: video playback is not supported by the libuvc backend.");
    return 0;
}

bool VideoStreamLibUVC::setPU(quint8 selector, quint16 value)
{
    uint8_t buf[2] = { static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8) };
    int r = uvc_set_ctrl(m_devh, PROCESSING_UNIT_ID, selector, buf, 2);
    usleep(CTRL_SETTLE_US);
    return r == 2;
}

int VideoStreamLibUVC::getPU(quint8 selector)
{
    uint8_t buf[2] = {0, 0};
    int r = uvc_get_ctrl(m_devh, PROCESSING_UNIT_ID, selector, buf, 2, UVC_GET_CUR);
    if (r < 0)
        return 0;
    return static_cast<qint16>(buf[0] | (buf[1] << 8));
}

// Flush queued I2C packets. Packs each packet into the CONTRAST/GAMMA/SHARPNESS
// UVC controls exactly like VideoStreamOCV::sendCommands().
void VideoStreamLibUVC::sendCommands()
{
    long key;
    QVector<quint8> packet;
    quint64 tempPacket;
    while (!sendCommandQueueOrder.isEmpty()) {
        key = sendCommandQueueOrder.first();
        packet = sendCommandQueue[key];
        if (packet.length() < 6) {
            tempPacket = (quint64)packet[0];
            tempPacket |= (((quint64)packet.length()) & 0xFF) << 8;
            for (int j = 1; j < packet.length(); j++)
                tempPacket |= ((quint64)packet[j]) << (8 * (j + 1));
        } else { // length == 6
            tempPacket = (quint64)packet[0] | 0x01;
            for (int j = 1; j < packet.length(); j++)
                tempPacket |= ((quint64)packet[j]) << (8 * j);
        }
        bool ok = setPU(SEL_CONTRAST,  (quint16)( tempPacket        & 0xFFFF));
        ok = setPU(SEL_GAMMA,    (quint16)((tempPacket >> 16) & 0xFFFF)) && ok;
        ok = setPU(SEL_SHARPNESS, (quint16)((tempPacket >> 32) & 0xFFFF)) && ok;
        if (!ok)
            qDebug() << "Send setting failed";
        sendCommandQueue.remove(key);
        sendCommandQueueOrder.removeFirst();
    }
}

void VideoStreamLibUVC::startStream()
{
    int idx = 0;
    int daqFrameNumOffset = 0;
    double norm;
    double w, x, y, z;
    double extTriggerLast = -1;
    double extTrigger;

    m_stopStreaming = false;

    if (!m_devh) {
        sendMessage("Error: Could not connect to video stream " + QString::number(m_cameraID));
        qDebug() << "Camera " << m_cameraID << " not open (libuvc).";
        return;
    }

    if (uvc_stream_open_ctrl(m_devh, &m_strmh, &m_streamCtrl) < 0 ||
        uvc_stream_start(m_strmh, nullptr, nullptr, 0) < 0) {
        sendMessage("Error: could not start libuvc stream for " + m_deviceName);
        closeStream();
        return;
    }

    // Enable continuous DAQ data/BNO register refresh (the app gates this on the
    // Record button via SATURATION=1; on Linux we need it live during Run so the
    // head-orientation registers update every frame, not just while recording).
    setPU(SEL_SATURATION, 0x0001);

    m_isStreaming = true;
    forever {
        if (m_stopStreaming) {
            m_isStreaming = false;
            break;
        }

        uvc_frame_t *frame = nullptr;
        // Timeout in us; ~2 frame periods, min 100ms.
        int32_t timeoutUs = 100000;
        uvc_error_t res = uvc_stream_get_frame(m_strmh, &frame, timeoutUs);

        if (res < 0 || frame == nullptr || frame->data == nullptr || frame->data_bytes == 0) {
            // Grab failed - attempt to reconnect, like the OpenCV backend.
            sendMessage("Warning: " + m_deviceName + " grab frame failed. Attempting to reconnect.");
            closeStream();
            QThread::msleep(1000);
            if (attemptReconnect()) {
                sendMessage("Warning: " + m_deviceName + " reconnected.");
                qDebug() << "Reconnect to camera" << m_cameraID;
            }
            continue;
        }

        timeStampBuffer[idx % frameBufferSize] = QDateTime().currentMSecsSinceEpoch();

        // libuvc gives raw YUYV; the Y plane is the Miniscope image.
        cv::Mat yuyv((int)frame->height, (int)frame->width, CV_8UC2, frame->data);
        if (m_isColor)
            cv::cvtColor(yuyv, frameBuffer[idx % frameBufferSize], cv::COLOR_YUV2BGR_YUYV);
        else
            cv::cvtColor(yuyv, frameBuffer[idx % frameBufferSize], cv::COLOR_YUV2GRAY_YUYV);

        if (m_trackExtTrigger) {
            if (extTriggerLast == -1) {
                extTriggerLast = getPU(SEL_GAMMA);
            } else {
                extTrigger = getPU(SEL_GAMMA);
                if (extTriggerLast != extTrigger) {
                    if (extTriggerLast == 0)
                        emit extTriggered(true);
                    else
                        emit extTriggered(false);
                }
                extTriggerLast = extTrigger;
            }
        }

        if (m_headOrientationStreamState) {
            // BNO output is a unit quaternion after a 2^14 division.
            w = getPU(SEL_SATURATION);
            x = getPU(SEL_HUE);
            y = getPU(SEL_GAIN);
            z = getPU(SEL_BRIGHTNESS);
            norm = sqrt(w * w + x * x + y * y + z * z);
            bnoBuffer[(idx % frameBufferSize) * 5 + 0] = w / 16384.0;
            bnoBuffer[(idx % frameBufferSize) * 5 + 1] = x / 16384.0;
            bnoBuffer[(idx % frameBufferSize) * 5 + 2] = y / 16384.0;
            bnoBuffer[(idx % frameBufferSize) * 5 + 3] = z / 16384.0;
            bnoBuffer[(idx % frameBufferSize) * 5 + 4] = abs((norm / 16384.0) - 1);
        }

        if (daqFrameNum != nullptr) {
            *daqFrameNum = getPU(SEL_CONTRAST) - daqFrameNumOffset;
            if (*m_acqFrameNum == 0)
                daqFrameNumOffset = *daqFrameNum - 1;
        }

        // Thread-safe buffer handoff (mirrors VideoStreamOCV).
        if (!freeFrames->tryAcquire()) {
            if (freeFrames->available() == 0) {
                sendMessage("Error: " + m_deviceName + " frame buffer is full. Frames will be lost!");
                QThread::msleep(100);
            }
        } else {
            m_acqFrameNum->operator++();
            idx++;
            emit newFrameAvailable(m_deviceName, *m_acqFrameNum);
            usedFrames->release();
        }

        // Process queued control changes (setPropertyI2C) and flush them.
        QCoreApplication::processEvents();
        if (!sendCommandQueue.isEmpty())
            sendCommands();
    }
    closeStream();
}

bool VideoStreamLibUVC::attemptReconnect()
{
    closeStream();
    closeDevice();
    if (!openByVideoIndex(m_cameraID))
        return false;
    if (!negotiateFormat())
        return false;
    sendSerdesModeCommands();
    if (uvc_stream_open_ctrl(m_devh, &m_strmh, &m_streamCtrl) < 0 ||
        uvc_stream_start(m_strmh, nullptr, nullptr, 0) < 0) {
        closeStream();
        return false;
    }
    setPU(SEL_SATURATION, 0x0001);
    QThread::msleep(500);
    emit requestInitCommands();
    return true;
}

void VideoStreamLibUVC::stopSteam()
{
    m_stopStreaming = true;
}

void VideoStreamLibUVC::setPropertyI2C(long preambleKey, QVector<quint8> packet)
{
    if (!sendCommandQueue.contains(preambleKey))
        sendCommandQueueOrder.append(preambleKey);
    sendCommandQueue[preambleKey] = packet;
}

void VideoStreamLibUVC::setExtTriggerTrackingState(bool state)
{
    m_trackExtTrigger = state;
}

void VideoStreamLibUVC::startRecording()
{
    // Data/BNO streaming is already enabled at stream start; nothing extra needed.
    if (m_devh)
        setPU(SEL_SATURATION, 0x0001);
}

void VideoStreamLibUVC::stopRecording()
{
    // Intentionally leave SATURATION=1 so head-orientation stays live during Run
    // after a recording stops. The data stream stops when streaming stops.
}

void VideoStreamLibUVC::openCamPropsDialog()
{
    // OpenCV/DirectShow-only feature (behaviour cameras); not applicable here.
}

#endif // HAVE_LIBUVC
