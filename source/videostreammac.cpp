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
// open its VideoControl interface. The which-device decision (including when
// falling back to "the one Miniscope attached" is safe, and when it must fail
// instead of guessing) lives in resolveControlTarget - see its declaration.
bool VideoStreamMac::openControlForIndex(int cameraID)
{
    QVector<quint32> attached;
    const auto miniscopes = UVCControlMac::enumerate(kUsbVendorId, kUsbProductId);
    for (const auto &dev : miniscopes)
        attached.append(dev.locationID);

    const ControlTarget target = resolveControlTarget(enumerateAvfCameras(), cameraID,
                                                      kUsbVendorId, kUsbProductId, attached);
    if (!target.warning.isEmpty())
        sendMessage("Warning: " + m_deviceName + ": " + target.warning);
    if (!target.ok) {
        sendMessage("Error: " + m_deviceName + ": " + target.error);
        return false;
    }

    if (m_control.open(kUsbVendorId, kUsbProductId, target.locationID))
        return true;
    // The resolved device would not open (e.g. a transient IOKit failure).
    // Retrying as "any Miniscope" is again only safe with exactly one attached.
    if (target.locationID != 0 && attached.size() == 1
        && m_control.open(kUsbVendorId, kUsbProductId, 0)) {
        sendMessage("Warning: " + m_deviceName + " control channel fell back to the "
                    "only Miniscope on the bus.");
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

    qInfo().nospace() << "[diag] " << m_deviceName << " AVF opened: native "
                      << cam->get(cv::CAP_PROP_FRAME_WIDTH) << "x"
                      << cam->get(cv::CAP_PROP_FRAME_HEIGHT) << " @ "
                      << cam->get(cv::CAP_PROP_FPS) << "fps; requesting "
                      << m_expectedWidth << "x" << m_expectedHeight;

    sendSerdesModeCommands();

    cam->set(cv::CAP_PROP_FRAME_WIDTH, m_expectedWidth);
    cam->set(cv::CAP_PROP_FRAME_HEIGHT, m_expectedHeight);
    QThread::msleep(500);

    qInfo().nospace() << "[diag] " << m_deviceName << " after set: "
                      << cam->get(cv::CAP_PROP_FRAME_WIDTH) << "x"
                      << cam->get(cv::CAP_PROP_FRAME_HEIGHT) << " @ "
                      << cam->get(cv::CAP_PROP_FPS) << "fps";
    return 1;
}

// Stall discriminator: when AVF frames stop, the control channel usually
// stays alive - so poll the DAQ's hardware frame counter. Advancing counter =
// the scope's video link is fine and the AVFoundation session died (format /
// session problem). Frozen counter = the scope's video pipeline itself went
// down (e.g. corrupted sensor/SERDES config - suspect driver-injected
// SET_CURs into the I2C tunnel).
void VideoStreamMac::logStallDiagnosis()
{
    const int c0 = getPU(SEL_CONTRAST);
    QThread::msleep(500);
    const int c1 = getPU(SEL_CONTRAST);
    QThread::msleep(500);
    const int c2 = getPU(SEL_CONTRAST);
    const bool advancing = (c1 != c0) || (c2 != c1);
    const QString verdict = advancing
        ? QStringLiteral("DAQ frame counter ADVANCING (%1 -> %2 -> %3): scope video link is "
                         "alive, AVFoundation session died")
        : QStringLiteral("DAQ frame counter FROZEN (%1 -> %2 -> %3): scope video pipeline is "
                         "down (sensor/SERDES state suspect)");
    const QString msg = verdict.arg(c0).arg(c1).arg(c2);
    qInfo().noquote() << "[diag]" << m_deviceName << msg;
    sendMessage("Diag: " + m_deviceName + " " + msg);
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

    qInfo() << "[diag]" << m_deviceName << "stream loop starting";

    m_isStreaming = true;
    forever {
        if (m_stopStreaming) {
            m_isStreaming = false;
            break;
        }

        if (!cam->grab() || !cam->retrieve(frame)) {
            qInfo() << "[diag]" << m_deviceName << "grab/retrieve returned false at frame"
                    << idx << "- running stall diagnosis";
            sendMessage("Warning: " + m_deviceName + " grab frame failed. Attempting to reconnect.");
            logStallDiagnosis();
            if (cam->isOpened())
                cam->release();
            QThread::msleep(1000);
            if (attemptReconnect()) {
                sendMessage("Warning: " + m_deviceName + " reconnected.");
                qDebug() << "Reconnect to camera" << m_cameraID;
            }
            continue;
        }

        if (idx == 0)
            qInfo().nospace() << "[diag] " << m_deviceName << " first frame: "
                              << frame.cols << "x" << frame.rows
                              << " channels=" << frame.channels()
                              << " daqFrameCounter=" << getPU(SEL_CONTRAST);
        else if (idx % 100 == 0)
            qInfo().nospace() << "[diag] " << m_deviceName << " heartbeat: acqFrame=" << idx
                              << " daqFrameCounter=" << getPU(SEL_CONTRAST)
                              << " ts=" << QDateTime::currentMSecsSinceEpoch();

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
