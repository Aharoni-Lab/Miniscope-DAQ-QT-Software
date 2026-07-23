#include "videostreammac.h"

#include <QDebug>
#include <QCoreApplication>
#include <QDateTime>
#include <QLoggingCategory>
#include <QThread>

#include <opencv2/imgproc.hpp>

#include "avfenumeratormac.h"

using namespace MiniscopeProtocol;

// Bench diagnostics (heartbeats, stall verdicts, pin lines). On by default;
// silence with QT_LOGGING_RULES="miniscope.diag=false".
Q_LOGGING_CATEGORY(msDiag, "miniscope.diag")

VideoStreamMac::VideoStreamMac(QObject *parent, int width, int height, double pixelClock) :
    VideoStreamBase(parent),
    m_cameraID(-1),
    m_deviceName(""),
    m_stopStreaming(false),
    m_headOrientationStreamState(false),
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
    m_pixelClock(pixelClock)
{
    m_control.setWriteSettleUs(kCtrlSettleUs);
}

VideoStreamMac::~VideoStreamMac()
{
    qDebug() << "Closing macOS hybrid video stream";
    m_grabber.release();
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
bool VideoStreamMac::openControlForIndex(int cameraID, const QVector<AvfCameraInfo> &cameras)
{
    QVector<quint32> attached;
    const auto miniscopes = UVCControlMac::enumerate(kUsbVendorId, kUsbProductId);
    for (const auto &dev : miniscopes)
        attached.append(dev.locationID);

    const ControlTarget target = resolveControlTarget(cameras, cameraID,
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

// Open the video stream pinned to the SAME physical device the control
// channel resolved, via its stable AVFoundation uniqueID. Never opens by list
// index: macOS reshuffles the camera list as devices come and go (iPhone
// Continuity Camera, hot-plugged webcams), and both bench failures came from
// an index resolving to a different camera by open time.
bool VideoStreamMac::openFrameStream(const QVector<AvfCameraInfo> &cameras)
{
    QString uniqueId = avfUniqueIdForLocation(cameras, m_control.locationID());
    if (uniqueId.isEmpty()) {
        // The control channel just opened this device over USB, so it exists;
        // AVFoundation's list can briefly lag a hot-plug. One settle+retry.
        QThread::msleep(500);
        uniqueId = avfUniqueIdForLocation(enumerateAvfCameras(), m_control.locationID());
    }
    if (uniqueId.isEmpty()) {
        sendMessage("Error: " + m_deviceName + ": the Miniscope's USB device (locationID 0x" +
                    QString::number(m_control.locationID(), 16) +
                    ") never appeared in the camera list.");
        return false;
    }
    if (!m_grabber.open(uniqueId, m_expectedWidth, m_expectedHeight)) {
        sendMessage("Error: could not open the video stream for " + m_deviceName + " (" +
                    m_grabber.lastError() + ")");
        return false;
    }
    qCInfo(msDiag).nospace() << m_deviceName << " frame stream pinned to uniqueID "
                             << uniqueId << " at " << m_expectedWidth << "x" << m_expectedHeight;
    return true;
}

int VideoStreamMac::connect2Camera(int cameraID)
{
    m_cameraID = cameraID;

    // One AVFoundation enumeration feeds both the control-channel policy and
    // the uniqueID pin (enumeration touches the camera-permission machinery
    // and costs tens of ms).
    const auto cameras = enumerateAvfCameras();

    // Control channel first: if the Miniscope isn't reachable over USB there
    // is no point opening a video stream to it.
    if (!openControlForIndex(cameraID, cameras))
        return 0;

    if (!openFrameStream(cameras)) {
        m_control.close();
        return 0;
    }

    sendSerdesModeCommands(m_pixelClock);   // ends with its own settle sleep
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
    quint16 c0 = 0, c1 = 0, c2 = 0;
    const bool ok0 = m_control.getCur(kProcessingUnitId, SEL_CONTRAST, &c0);
    QThread::msleep(500);
    const bool ok1 = m_control.getCur(kProcessingUnitId, SEL_CONTRAST, &c1);
    QThread::msleep(500);
    const bool ok2 = m_control.getCur(kProcessingUnitId, SEL_CONTRAST, &c2);

    QString msg;
    if (!ok0 && !ok1 && !ok2) {
        // The control channel is gone too: this is a USB-level disconnect
        // (cable/power/bus), not a video-layer problem.
        msg = QStringLiteral("control channel unreachable as well (%1): the DAQ has "
                             "disconnected at the USB level - check cable/hub/power")
                  .arg(m_control.lastError());
    } else if (c0 != c1 || c1 != c2) {
        msg = QStringLiteral("DAQ frame counter ADVANCING (%1 -> %2 -> %3): scope video link "
                             "is alive, AVFoundation session died")
                  .arg(c0).arg(c1).arg(c2);
    } else {
        msg = QStringLiteral("DAQ frame counter FROZEN (%1 -> %2 -> %3): scope video pipeline "
                             "is down (sensor/SERDES state suspect)")
                  .arg(c0).arg(c1).arg(c2);
    }
    qCInfo(msDiag).noquote() << m_deviceName << msg;
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

    if (!m_grabber.isOpened()) {
        sendMessage("Error: Could not connect to video stream " + QString::number(m_cameraID));
        qDebug() << "Camera " << m_cameraID << " not open (macOS hybrid).";
        return;
    }

    // Enable continuous DAQ data/BNO register refresh now rather than on the
    // Record button, so head orientation is live during Run (same reasoning
    // as the libuvc backend).
    setPU(SEL_SATURATION, 0x0001);

    qCInfo(msDiag) << m_deviceName << "stream loop starting";

    int reconnectAttempts = 0;
    forever {
        if (m_stopStreaming)
            break;

        if (!m_grabber.read(frame)) {
            // Diagnose and message on the FIRST failure of a stall episode;
            // later attempts back off quietly (a device that fell off the bus
            // can take a while to come back, and every attempt already logs).
            if (reconnectAttempts == 0) {
                qCInfo(msDiag) << m_deviceName << "no frame at" << idx << "("
                               << m_grabber.lastError() << ") - running stall diagnosis";
                sendMessage("Warning: " + m_deviceName + " grab frame failed. Attempting to reconnect.");
                logStallDiagnosis();
            }
            reconnectAttempts++;
            m_grabber.release();
            QThread::msleep(qMin(1000 * reconnectAttempts, 5000));
            QCoreApplication::processEvents();   // keep stopSteam() deliverable while down
            if (m_stopStreaming)
                continue;
            if (attemptReconnect()) {
                sendMessage("Warning: " + m_deviceName + " reconnected (after " +
                            QString::number(reconnectAttempts) + " attempts).");
                qDebug() << "Reconnect to camera" << m_cameraID;
                reconnectAttempts = 0;
            }
            continue;
        }

        // Reserve the ring-buffer slot BEFORE any per-frame work: a full
        // buffer drops this frame anyway, so the USB control reads (~6 ms
        // each while streaming) and the color conversion would only deepen
        // the backpressure - and acquiring first guarantees a slot is never
        // overwritten while DataSaver still owns it.
        if (!freeFrames->tryAcquire()) {
            if (freeFrames->available() == 0) {
                sendMessage("Error: " + m_deviceName + " frame buffer is full. Frames will be lost!");
                QThread::msleep(100);
            }
        } else {
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
                // Diagnostics reuse this read - never a second GET_CUR.
                if (idx % 100 == 0)
                    qCInfo(msDiag).nospace()
                        << m_deviceName << (idx == 0 ? " first frame: " : " heartbeat: ")
                        << frame.cols << "x" << frame.rows << " acqFrame=" << idx
                        << " daqFrameCounter=" << (*daqFrameNum + daqFrameNumOffset)
                        << " ts=" << timeStampBuffer[idx % frameBufferSize];
            }

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
    m_grabber.release();
    m_control.close();
}

bool VideoStreamMac::attemptReconnect()
{
    m_control.close();
    // Fresh enumeration: the device may have re-enumerated (which can be
    // exactly why we are reconnecting).
    const auto cameras = enumerateAvfCameras();
    if (!openControlForIndex(m_cameraID, cameras))
        return false;
    if (!openFrameStream(cameras))
        return false;
    sendSerdesModeCommands(m_pixelClock);
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
