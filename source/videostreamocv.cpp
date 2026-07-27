#include "videostreamocv.h"
#include "miniscopeprotocol.h"
#include "monotonicclock.h"
#ifdef Q_OS_MACOS
#include "avfenumeratormac.h"
#endif
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <QDebug>
#include <QCoreApplication>
#include <QVector>
#include <QThread>

VideoStreamOCV::VideoStreamOCV(QObject *parent, int width, int height, double pixelClock) :
    VideoStreamBase(parent, width, height, pixelClock),
    cam(nullptr),
    m_connectionType("")
{

}

VideoStreamOCV::~VideoStreamOCV() {
    qDebug() << "Closing video stream";
    if (cam != nullptr && cam->isOpened())
        cam->release();
#ifdef Q_OS_MACOS
    m_grabber.release();
#endif
}

int VideoStreamOCV::connect2Camera(int cameraID) {
    int connectionState = 0;
    m_cameraID = cameraID;

#ifdef Q_OS_MACOS
    // OpenCV's AVFoundation backend can only open a camera by LIST INDEX, and
    // macOS reshuffles that list as devices come and go - after a disconnect,
    // an index reopen can silently bind a different physical camera. Resolve
    // the config's deviceID to the camera's stable uniqueID once, here, and
    // pin the capture session to it (same approach as the Miniscope's hybrid
    // backend in videostreammac.cpp).
    const WebcamTarget target = resolveWebcamTarget(enumerateAvfCameras(), cameraID);
    if (!target.ok) {
        sendMessage("Error: " + m_deviceName + ": " + target.error);
        qDebug() << m_deviceName << "webcam resolve failed:" << target.error;
        return 0;
    }
    if (!m_grabber.open(target.uniqueID, qMax(0, m_expectedWidth), qMax(0, m_expectedHeight))) {
        sendMessage("Error: could not open the video stream for " + m_deviceName +
                    " (" + m_grabber.lastError() + ")");
        return 0;
    }
    m_avfUniqueID = target.uniqueID;
    m_avfName = target.name;
    m_connectionType = "AVF";
    qDebug().nospace() << m_deviceName << " pinned to \"" << m_avfName
                       << "\" uniqueID " << m_avfUniqueID;
    return 1;
#endif

    cam = new cv::VideoCapture;

    auto apiPreference = cv::CAP_ANY;
    QString apiName = "OTHER";
#ifdef Q_OS_LINUX
    apiPreference = cv::CAP_V4L2;
    apiName = QStringLiteral("V4L");
#elif defined(Q_OS_WINDOWS)
    // Try connecting using DShow backend
    apiPreference = cv::CAP_DSHOW;
    apiName = QStringLiteral("DSHOW");
#endif

    if (cam->open(m_cameraID, apiPreference)) {
        // we got our preferred backend!
        connectionState = 1;
        m_connectionType = apiName;
    }
    else {
        // connecting again using default backend
        if (cam->open(m_cameraID)) {
            connectionState = 2;
            m_connectionType = "OTHER";
        }
    }
    // The SERDES mode must be set before any other SERDES traffic (TI 913/914).
    if (connectionState != 0)
        sendSerdesModeCommands();

    if (connectionState != 0) {
         cam->set(cv::CAP_PROP_FRAME_WIDTH, m_expectedWidth);
         cam->set(cv::CAP_PROP_FRAME_HEIGHT, m_expectedHeight);
         QThread::msleep(500);
    }
//    qDebug() <<  "Camera capture backend is" << QString::fromStdString (cam->getBackendName());
    return connectionState;


}

int VideoStreamOCV::connect2Video(QString folderPath, QString filePrefix, float playbackFPS)
{
    m_playbackFolderPath = folderPath;
    m_playbackFilePrefix = filePrefix;
    m_playbackFPS = playbackFPS;
    m_playbackFileIndex = 0;

    QString firstVideoFile = m_playbackFolderPath + "/" + m_playbackFilePrefix + QString::number(m_playbackFileIndex) + ".avi";
    cam = new cv::VideoCapture;
    if (cam->open(firstVideoFile.toStdString())) {
        QThread::msleep(500);
        m_connectionType = "videoFile";
        return 3;
    }
    else
        return 0;
}

void VideoStreamOCV::startStream()
{
    resetStreamState();
    m_transientFailures = 0;
    ReconnectBackoff backoff;
    cv::Mat frame;
    qint64 timestamp = 0;

    bool streamOpen = (cam != nullptr && cam->isOpened());
#ifdef Q_OS_MACOS
    if (m_connectionType == "AVF")
        streamOpen = m_grabber.isOpened();
#endif
    if (!streamOpen) {
        sendMessage("Error: Could not connect to video stream " + QString::number(m_cameraID));
        qDebug() << "Camera " << m_cameraID << " failed to open.";
        return;
    }

    forever {
        if (m_stopStreaming)
            break;

        // Get new frame and handle disconnects
#ifdef Q_OS_MACOS
        if (m_connectionType == "AVF") {
            if (!m_grabber.read(frame)) {
                m_grabber.release();
                runReconnectCycle(backoff, "grab frame");
                continue;
            }
            timestamp = monotonicTimeMs();
            backoff.reset();
        }
        else
#endif
        if (m_connectionType != "videoFile") {
            // A grab/retrieve can fail transiently while the device is fine -
            // bench-observed on Windows/DSHOW whenever a window is interactively
            // resized (GPU/DWM contention during the GL swapchain rebuild).
            // Releasing the camera on the first failure turns that blip into a
            // full disconnect: seconds of reconnect backoff, frozen video, and
            // the persistent "disconnected" indicator. So retry briefly in
            // place, and only run the reconnect cycle when the failure persists
            // for ~1 s (a genuinely unplugged device still reconnects, just one
            // second later).
            const char *failedStep = nullptr;
            if (!cam->grab()) {
                failedStep = "grab frame";
            } else {
                timestamp = monotonicTimeMs();
                if (!cam->retrieve(frame))
                    failedStep = "retrieve frame";
            }
            if (failedStep) {
                if (++m_transientFailures < kTransientFailureLimit) {
                    QThread::msleep(kTransientRetryDelayMs);
                    continue;
                }
                m_transientFailures = 0;
                if (cam->isOpened()) {
                    qDebug() << failedStep << "failed: Releasing cam" << m_cameraID;
                    cam->release();
                    qDebug() << "Released cam" << m_cameraID;
                }
                runReconnectCycle(backoff, QString::fromLatin1(failedStep));
                continue;
            }
            m_transientFailures = 0;
            backoff.reset();
        }
        else {
            // Video-file playback: pace to the requested FPS.
            QThread::msleep(ulong(1000.0 / m_playbackFPS));
            timestamp = monotonicTimeMs();
            if (!cam->read(frame)) {
                // Try next file before fully giving up. End playback with a
                // break, not a return: break falls through to the
                // cam->release() after the loop, a return leaks the handle.
                m_playbackFileIndex++;
                qDebug() << "FILE INDEX" << m_playbackFileIndex;
                const QString fileName = m_playbackFolderPath + "/" + m_playbackFilePrefix +
                                         QString::number(m_playbackFileIndex) + ".avi";
                cam->release();
                if (!cam->open(fileName.toStdString()) || !cam->read(frame)) {
                    sendMessage(m_deviceName + " playback reached the end of the recording.");
                    break;
                }
            }
        }

        commitFrame(frame, timestamp);
        serviceCommandQueue();
    }
    if (cam != nullptr)
        cam->release();
#ifdef Q_OS_MACOS
    m_grabber.release();
#endif
}

void VideoStreamOCV::startRecording()
{
    if (cam != nullptr && cam->isOpened()){
        cam->set(cv::CAP_PROP_SATURATION, 0x0001);
    }
}

void VideoStreamOCV::stopRecording()
{
    if (cam != nullptr && cam->isOpened()){
        cam->set(cv::CAP_PROP_SATURATION, 0x0000);
    }
}

void VideoStreamOCV::openCamPropsDialog()
{
    if (cam != nullptr && cam->isOpened()){
        cam->set(cv::CAP_PROP_SETTINGS, 0);
    }
}

static bool camSetProperty(cv::VideoCapture *cam, int propId, double value)
{
    const auto ret = cam->set(propId, value);
    // Linux apparently is faster at USB communication than Windows, and since our DAQ
    // board is slow at clearing data from its control endpoint, not waiting a bit before
    // sending the next command will result in the old command being overridden (which breaks
    // our packet layout)
    // Waiting >100µs seems to generally work. We call the wait function on all platforms,
    // just in case some computers on Windows also manage to communicate with similar speeds then
    // Windows, but keep in mind that Windows may not be able to wait with microsecond accuracy and
    // may wait 1ms instead of our set value.

    // TODO: Make sure this doesn't break things on Windows. It really shouldn't!
    QThread::usleep(128);
    return ret;
}

// The UVC selector each control word travels on, as this backend's OpenCV
// property. OpenCV property IDs and UVC PU selectors are different vocabularies
// for the same controls; this is the single translation point (both directions:
// I2C-word writes and the per-frame register reads).
static int capPropForSelector(quint8 selector)
{
    switch (selector) {
    case MiniscopeProtocol::SEL_BRIGHTNESS: return cv::CAP_PROP_BRIGHTNESS;
    case MiniscopeProtocol::SEL_CONTRAST:   return cv::CAP_PROP_CONTRAST;
    case MiniscopeProtocol::SEL_GAIN:       return cv::CAP_PROP_GAIN;
    case MiniscopeProtocol::SEL_HUE:        return cv::CAP_PROP_HUE;
    case MiniscopeProtocol::SEL_SATURATION: return cv::CAP_PROP_SATURATION;
    case MiniscopeProtocol::SEL_SHARPNESS:  return cv::CAP_PROP_SHARPNESS;
    case MiniscopeProtocol::SEL_GAMMA:      return cv::CAP_PROP_GAMMA;
    default:                                return -1;
    }
}

bool VideoStreamOCV::writeControlWord(quint8 selector, quint16 word)
{
    return camSetProperty(cam, capPropForSelector(selector), word);
}

bool VideoStreamOCV::readControl(quint8 selector, quint16 *value)
{
    // cv::VideoCapture::get has no failure signal (it returns 0.0 for
    // unsupported properties), so the only detectable no-control case is the
    // AVF webcam path, where there is no cv capture at all.
    if (cam == nullptr)
        return false;
    *value = quint16(qint32(cam->get(capPropForSelector(selector))));
    return true;
}

void VideoStreamOCV::sendCommands()
{
    if (cam == nullptr) {
        // AVF path: AVFoundation exposes no UVC controls, so there is no
        // control channel to deliver queued commands to. Empty the queue
        // rather than leave it growing.
        m_commandQueue.flush([](quint8, quint16) { return true; });
        qDebug() << m_deviceName << "dropped queued device commands (no control channel on this backend)";
        return;
    }
    VideoStreamBase::sendCommands();
}

bool VideoStreamOCV::attemptReconnect()
{
#ifdef Q_OS_MACOS
    if (m_connectionType == "AVF") {
        // Re-resolve by uniqueID, NEVER by index: the camera list may have
        // reshuffled while we were down, and an index reopen can bind a
        // different physical camera. Either the same physical camera is back
        // (replug on the same port keeps its uniqueID) or we keep failing
        // loudly until it is.
        const auto cameras = enumerateAvfCameras();
        bool present = false;
        for (const AvfCameraInfo &c : cameras) {
            if (c.uniqueID == m_avfUniqueID) {
                present = true;
                break;
            }
        }
        if (!present) {
            qDebug().nospace() << m_deviceName << ": camera \"" << m_avfName
                               << "\" (uniqueID " << m_avfUniqueID
                               << ") is no longer connected; waiting for it to return.";
            return false;
        }
        return m_grabber.open(m_avfUniqueID, qMax(0, m_expectedWidth), qMax(0, m_expectedHeight));
    }
#endif
    // TODO: handle quitting nicely when stuck in this loop
    if (m_connectionType == "V4L") {
        // Pre-R4 this case was missing entirely, so a Linux camera that
        // dropped could never reconnect - the loop retried forever.
        if (!cam->open(m_cameraID, cv::CAP_V4L2))
            return false;
    }
    else if (m_connectionType == "DSHOW") {
        if (!cam->open(m_cameraID, cv::CAP_DSHOW))
            return false;
    }
    else if (m_connectionType == "OTHER") {
        if (!cam->open(m_cameraID))
            return false;
    }
    else {
        return false;
    }
    sendSerdesModeCommands();
    cam->set(cv::CAP_PROP_FRAME_WIDTH, m_expectedWidth);
    cam->set(cv::CAP_PROP_FRAME_HEIGHT, m_expectedHeight);
    QThread::msleep(500);
    emit requestInitCommands();
    return true;
}
