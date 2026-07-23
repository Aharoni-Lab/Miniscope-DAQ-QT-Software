#include "videostreammac.h"

#include <QDebug>
#include <QCoreApplication>
#include <QDateTime>
#include <QThread>

#include <opencv2/imgproc.hpp>

#include "avfenumeratormac.h"

using namespace MiniscopeProtocol;

VideoStreamMac::VideoStreamMac(QObject *parent, int width, int height, double pixelClock) :
    VideoStreamBase(parent),
    m_cameraID(-1),
    m_deviceName(""),
    cam(nullptr),
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
    m_control.setWriteSettleUs(kCtrlSettleUs);
}

VideoStreamMac::~VideoStreamMac()
{
    qDebug() << "Closing macOS hybrid video stream";
    if (cam && cam->isOpened())
        cam->release();
    delete cam;
    m_control.close();
}

void VideoStreamMac::setBufferParameters(cv::Mat *frameBuf, qint64 *tsBuf, float *bnoBuf,
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

// Resolve the AVFoundation/OpenCV device index to the exact USB device and
// open its VideoControl interface (robust with multiple Miniscopes: the
// index's uniqueID carries the USB locationID). Falls back to the first
// Miniscope by VID/PID when the index can't be resolved.
bool VideoStreamMac::openControlForIndex(int cameraID)
{
    quint32 locationID = 0;
    const auto cameras = enumerateAvfCameras();
    if (cameraID >= 0 && cameraID < cameras.size() && cameras[cameraID].isUsb) {
        locationID = cameras[cameraID].locationID;
        if (cameras[cameraID].vid != kUsbVendorId || cameras[cameraID].pid != kUsbProductId)
            sendMessage("Warning: deviceID " + QString::number(cameraID) + " (" +
                        cameras[cameraID].name + ") does not look like a Miniscope DAQ.");
    }

    if (m_control.open(kUsbVendorId, kUsbProductId, locationID))
        return true;
    if (locationID != 0 && m_control.open(kUsbVendorId, kUsbProductId, 0)) {
        sendMessage("Warning: " + m_deviceName + " control channel fell back to the "
                    "first Miniscope on the bus.");
        return true;
    }
    sendMessage("Error: could not open the Miniscope control channel for " + m_deviceName +
                " (" + m_control.lastError() + ")");
    return false;
}

void VideoStreamMac::sendSerdesModeCommands()
{
    const auto packets = serdesModePackets(m_pixelClock);
    if (packets.isEmpty())
        return;
    for (int i = 0; i < packets.size(); i++)
        setPropertyI2C(i, packets[i]);
    sendCommands();
    QThread::msleep(500);
}

int VideoStreamMac::connect2Camera(int cameraID)
{
    m_cameraID = cameraID;

    // Control channel first: if the Miniscope isn't reachable over USB there
    // is no point opening a video stream to it.
    if (!openControlForIndex(cameraID))
        return 0;

    cam = new cv::VideoCapture;
    if (!cam->open(cameraID, cv::CAP_AVFOUNDATION)) {
        sendMessage("Error: could not open AVFoundation stream for " + m_deviceName +
                    " (deviceID " + QString::number(cameraID) + ")");
        m_control.close();
        return 0;
    }
    m_connectionType = "AVF";

    sendSerdesModeCommands();

    cam->set(cv::CAP_PROP_FRAME_WIDTH, m_expectedWidth);
    cam->set(cv::CAP_PROP_FRAME_HEIGHT, m_expectedHeight);
    QThread::msleep(500);
    return 1;
}

int VideoStreamMac::connect2Video(QString, QString, float)
{
    // Video-file playback always uses the OpenCV backend; not supported here.
    sendMessage("Error: video playback is not supported by the macOS hybrid backend.");
    return 0;
}

bool VideoStreamMac::setPU(quint8 selector, quint16 value)
{
    return m_control.setCur(kProcessingUnitId, selector, value);
}

int VideoStreamMac::getPU(quint8 selector)
{
    quint16 value = 0;
    if (!m_control.getCur(kProcessingUnitId, selector, &value))
        return 0;
    return static_cast<qint16>(value);
}

// Flush queued I2C packets to the device via UVC SET_CUR.
void VideoStreamMac::sendCommands()
{
    if (!m_commandQueue.flush([this](quint8 sel, quint16 word) { return setPU(sel, word); }))
        qDebug() << "Send setting failed";
}

void VideoStreamMac::startStream()
{
    int idx = 0;
    int daqFrameNumOffset = 0;
    double extTriggerLast = -1;
    double extTrigger;
    cv::Mat frame;

    m_stopStreaming = false;

    if (!cam || !cam->isOpened()) {
        sendMessage("Error: Could not connect to video stream " + QString::number(m_cameraID));
        qDebug() << "Camera " << m_cameraID << " not open (macOS hybrid).";
        return;
    }

    // Enable continuous DAQ data/BNO register refresh now rather than on the
    // Record button, so head orientation is live during Run (same reasoning
    // as the libuvc backend).
    setPU(SEL_SATURATION, 0x0001);

    m_isStreaming = true;
    forever {
        if (m_stopStreaming) {
            m_isStreaming = false;
            break;
        }

        if (!cam->grab() || !cam->retrieve(frame)) {
            sendMessage("Warning: " + m_deviceName + " grab frame failed. Attempting to reconnect.");
            if (cam->isOpened())
                cam->release();
            QThread::msleep(1000);
            if (attemptReconnect()) {
                sendMessage("Warning: " + m_deviceName + " reconnected.");
                qDebug() << "Reconnect to camera" << m_cameraID;
            }
            continue;
        }

        timeStampBuffer[idx % frameBufferSize] = QDateTime().currentMSecsSinceEpoch();

        if (m_isColor)
            frame.copyTo(frameBuffer[idx % frameBufferSize]);
        else
            cv::cvtColor(frame, frameBuffer[idx % frameBufferSize], cv::COLOR_BGR2GRAY);

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
            qint16 quat[4];   // w, x, y, z per kBnoSelectors order
            for (int i = 0; i < 4; i++)
                quat[i] = static_cast<qint16>(getPU(kBnoSelectors[i]));
            unpackBnoQuaternion(quat[0], quat[1], quat[2], quat[3],
                                &bnoBuffer[(idx % frameBufferSize) * 5]);
        }

        if (daqFrameNum != nullptr) {
            *daqFrameNum = getPU(SEL_CONTRAST) - daqFrameNumOffset;
            if (*m_acqFrameNum == 0)
                daqFrameNumOffset = *daqFrameNum - 1;
        }

        // Thread-safe buffer handoff (mirrors the other backends).
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
        if (!m_commandQueue.isEmpty())
            sendCommands();
    }
    cam->release();
    m_control.close();
}

bool VideoStreamMac::attemptReconnect()
{
    m_control.close();
    if (!openControlForIndex(m_cameraID))
        return false;
    if (!cam->open(m_cameraID, cv::CAP_AVFOUNDATION))
        return false;
    sendSerdesModeCommands();
    cam->set(cv::CAP_PROP_FRAME_WIDTH, m_expectedWidth);
    cam->set(cv::CAP_PROP_FRAME_HEIGHT, m_expectedHeight);
    QThread::msleep(500);
    setPU(SEL_SATURATION, 0x0001);
    emit requestInitCommands();
    return true;
}

void VideoStreamMac::stopSteam()
{
    m_stopStreaming = true;
}

void VideoStreamMac::setPropertyI2C(long preambleKey, QVector<quint8> packet)
{
    m_commandQueue.set(preambleKey, packet);
}

void VideoStreamMac::setExtTriggerTrackingState(bool state)
{
    m_trackExtTrigger = state;
}

void VideoStreamMac::startRecording()
{
    // Data/BNO streaming is already enabled at stream start; nothing extra needed.
    setPU(SEL_SATURATION, 0x0001);
}

void VideoStreamMac::stopRecording()
{
    // Intentionally leave SATURATION=1 so head orientation stays live during
    // Run after a recording stops (matches the libuvc backend).
}

void VideoStreamMac::openCamPropsDialog()
{
    // OpenCV/DirectShow-only feature (behaviour cameras); not applicable here.
}
