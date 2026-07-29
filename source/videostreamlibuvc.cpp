#include "videostreamlibuvc.h"

#ifdef HAVE_LIBUVC

#include "miniscopeprotocol.h"
#include "monotonicclock.h"
#include "uvcrequest.h"

using namespace MiniscopeProtocol;   // SEL_* selectors, kProcessingUnitId, VID/PID

#include <QDebug>
#include <QString>
#include <QThread>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <climits>
#include <cstdlib>
#include <unistd.h>

VideoStreamLibUVC::VideoStreamLibUVC(QObject *parent, int width, int height, double pixelClock) :
    VideoStreamBase(parent, width > 0 ? width : 608, height > 0 ? height : 608, pixelClock),
    m_ctx(nullptr),
    m_dev(nullptr),
    m_devh(nullptr),
    m_strmh(nullptr),
    m_negotiatedFps(0)
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
        if (uvc_find_device(m_ctx, &m_dev, kUsbVendorId, kUsbProductId, nullptr) < 0)
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
    sendSerdesModeCommands();
    return 1;
}

bool VideoStreamLibUVC::writeControlWord(quint8 selector, quint16 word)
{
    uint8_t buf[2];
    UVCRequest::encodeLE16(word, buf);
    int r = uvc_set_ctrl(m_devh, kProcessingUnitId, selector, buf, 2);
    usleep(kCtrlSettleUs);   // let the DAQ's slow control endpoint clear the write
    return r == 2;
}

bool VideoStreamLibUVC::readControl(quint8 selector, quint16 *value)
{
    uint8_t buf[2] = {0, 0};
    int r = uvc_get_ctrl(m_devh, kProcessingUnitId, selector, buf, 2, UVC_GET_CUR);
    if (r != 2)
        return false;
    *value = UVCRequest::decodeLE16(buf);
    return true;
}

void VideoStreamLibUVC::convertToSlot(const cv::Mat &frame, cv::Mat &slot)
{
    // libuvc gives raw YUYV; the Y plane is the Miniscope image.
    cv::cvtColor(frame, slot, m_isColor ? cv::COLOR_YUV2BGR_YUYV : cv::COLOR_YUV2GRAY_YUYV);
}

void VideoStreamLibUVC::startStream()
{
    resetStreamState();
    ReconnectBackoff backoff;

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
    writeControlWord(SEL_SATURATION, 0x0001);

    forever {
        if (m_stopStreaming)
            break;
        // Nothing drains the ring buffer until the session's DataSaver exists.
        if (heldForSession())
            continue;

        uvc_frame_t *frame = nullptr;
        // Timeout in us; ~2 frame periods, min 100ms.
        int32_t timeoutUs = 100000;
        uvc_error_t res = uvc_stream_get_frame(m_strmh, &frame, timeoutUs);

        if (res < 0 || frame == nullptr || frame->data == nullptr || frame->data_bytes == 0) {
            closeStream();
            runReconnectCycle(backoff, "grab frame");
            continue;
        }
        backoff.reset();

        const cv::Mat yuyv(int(frame->height), int(frame->width), CV_8UC2, frame->data);
        commitFrame(yuyv, monotonicTimeMs());
        serviceCommandQueue();
    }
    // Stream loop only exits on stopStream() (device window closing). Release
    // the device so uvc_close re-attaches the kernel uvcvideo driver and
    // /dev/videoN comes back for the next run / for Scan Devices.
    closeStream();
    closeDevice();
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
    writeControlWord(SEL_SATURATION, 0x0001);
    QThread::msleep(500);
    emit requestInitCommands();
    return true;
}

void VideoStreamLibUVC::startRecording()
{
    // Data/BNO streaming is already enabled at stream start; nothing extra needed.
    if (m_devh)
        writeControlWord(SEL_SATURATION, 0x0001);
}

void VideoStreamLibUVC::stopRecording()
{
    // Intentionally leave SATURATION=1 so head-orientation stays live during Run
    // after a recording stops. The data stream stops when streaming stops.
}

#endif // HAVE_LIBUVC
