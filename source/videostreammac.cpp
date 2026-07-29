#include "videostreammac.h"
#include "monotonicclock.h"

#include <QDebug>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QThread>

#include "avfenumeratormac.h"

using namespace MiniscopeProtocol;

VideoStreamMac::VideoStreamMac(QObject *parent, int width, int height, double pixelClock) :
    VideoStreamBase(parent, width > 0 ? width : 608, height > 0 ? height : 608, pixelClock)
{
    m_control.setWriteSettleUs(kCtrlSettleUs);
}

VideoStreamMac::~VideoStreamMac()
{
    qDebug() << "Closing macOS hybrid video stream";
    m_grabber.release();
    m_control.close();
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

    sendSerdesModeCommands();   // ends with its own settle sleep
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

void VideoStreamMac::diagnoseStreamFailure()
{
    qCInfo(msDiag) << m_deviceName << "no frame at" << m_streamIdx << "("
                   << m_grabber.lastError() << ") - running stall diagnosis";
    logStallDiagnosis();
}

bool VideoStreamMac::writeControlWord(quint8 selector, quint16 word)
{
    return m_control.setCur(kProcessingUnitId, selector, word);
}

bool VideoStreamMac::readControl(quint8 selector, quint16 *value)
{
    return m_control.getCur(kProcessingUnitId, selector, value);
}

void VideoStreamMac::startStream()
{
    resetStreamState();
    ReconnectBackoff backoff;
    cv::Mat frame;

    if (!m_grabber.isOpened()) {
        sendMessage("Error: Could not connect to video stream " + QString::number(m_cameraID));
        qDebug() << "Camera " << m_cameraID << " not open (macOS hybrid).";
        return;
    }

    // Enable continuous DAQ data/BNO register refresh now rather than on the
    // Record button, so head orientation is live during Run (same reasoning
    // as the libuvc backend).
    writeControlWord(SEL_SATURATION, 0x0001);

    qCInfo(msDiag) << m_deviceName << "stream loop starting";

    forever {
        if (m_stopStreaming)
            break;
        // Nothing drains the ring buffer until the session's DataSaver exists.
        if (heldForSession())
            continue;

        if (!m_grabber.read(frame)) {
            // The grabber is NOT released here: the stall diagnosis inside
            // runReconnectCycle polls the DAQ frame counter, which only
            // advances while the device streams - releasing the session first
            // would freeze it and flip an "AVF session died" verdict into a
            // false "scope video pipeline down". attemptReconnect() releases
            // before reopening.
            runReconnectCycle(backoff, "grab frame");
            continue;
        }
        backoff.reset();

        commitFrame(frame, monotonicTimeMs());
        serviceCommandQueue();
    }
    m_grabber.release();
    m_control.close();
}

bool VideoStreamMac::attemptReconnect()
{
    m_grabber.release();
    m_control.close();
    // Fresh enumeration: the device may have re-enumerated (which can be
    // exactly why we are reconnecting).
    const auto cameras = enumerateAvfCameras();
    if (!openControlForIndex(m_cameraID, cameras))
        return false;
    if (!openFrameStream(cameras))
        return false;
    sendSerdesModeCommands();
    writeControlWord(SEL_SATURATION, 0x0001);
    emit requestInitCommands();
    return true;
}

void VideoStreamMac::startRecording()
{
    // Data/BNO streaming is already enabled at stream start; nothing extra needed.
    writeControlWord(SEL_SATURATION, 0x0001);
}

void VideoStreamMac::stopRecording()
{
    // Intentionally leave SATURATION=1 so head orientation stays live during
    // Run after a recording stops (matches the libuvc backend).
}
