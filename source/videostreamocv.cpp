#include "videostreamocv.h"
#include "miniscopeprotocol.h"
#include "monotonicclock.h"
#ifdef Q_OS_MACOS
#include "avfenumeratormac.h"
#endif
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <QDebug>
#include <QAtomicInt>
#include <QCoreApplication>
#include <QVector>
#include <QDateTime>
#include <QThread>
#include <QtMath>

VideoStreamOCV::VideoStreamOCV(QObject *parent, int width, int height, double pixelClock) :
    VideoStreamBase(parent),
    m_cameraID(-1),
    m_deviceName(""),
    cam(nullptr),
    m_stopStreaming(false),
    m_headOrientationStreamState(false),
    m_headOrientationFilterState(false),
    m_isColor(false),
    m_trackExtTrigger(false),
    m_expectedWidth(width),
    m_expectedHeight(height),
    m_pixelClock(pixelClock),
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
        sendSerdesModeCommands(m_pixelClock);

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

void VideoStreamOCV::setBufferParameters(cv::Mat *frameBuf, qint64 *tsBuf, float *bnoBuf,
                                         qint64 *daqFrameNumBuf,
                                         int bufferSize, QSemaphore *freeFramesS, QSemaphore *usedFramesS,
                                         QAtomicInt *acqFrameNum, QAtomicInt *daqFrameNumber){
    frameBuffer = frameBuf;
    timeStampBuffer = tsBuf;
    daqFrameNumBuffer = daqFrameNumBuf;
    frameBufferSize = bufferSize;
    bnoBuffer = bnoBuf;
    freeFrames = freeFramesS;
    usedFrames = usedFramesS;
    m_acqFrameNum = acqFrameNum;
    daqFrameNum = daqFrameNumber;

}

void VideoStreamOCV::startStream()
{
    QString fileName;
    int idx = 0;
    int daqFrameNumOffset = 0;
    double extTriggerLast = -1;
    double extTrigger;
    bool status = false;
    qint64 timestamp = 0;
    cv::Mat frame;

    m_stopStreaming = false;

    bool streamOpen = (cam != nullptr && cam->isOpened());
#ifdef Q_OS_MACOS
    int reconnectAttempts = 0;
    if (m_connectionType == "AVF")
        streamOpen = m_grabber.isOpened();
#endif

    if (streamOpen) {
        m_isStreaming = true;
        forever {

            if (m_stopStreaming == true) {
                m_isStreaming = false;
                break;
            }

            status = true;
            // Get new frame and handle disconnects
#ifdef Q_OS_MACOS
            if (m_connectionType == "AVF") {
                if (!m_grabber.read(frame)) {
                    status = false;
                    // Message on the FIRST failure of a stall episode; later
                    // attempts back off quietly (a device that fell off the
                    // bus can take a while to come back).
                    if (reconnectAttempts == 0)
                        sendMessage("Warning: " + m_deviceName + " grab frame failed. Attempting to reconnect.");
                    reconnectAttempts++;
                    m_grabber.release();
                    QThread::msleep(qMin(1000 * reconnectAttempts, 5000));
                    QCoreApplication::processEvents();   // keep stopSteam() deliverable while down
                    if (m_stopStreaming)
                        continue;
                    if (attemptReconnect()) {
                        sendMessage("Warning: " + m_deviceName + " reconnected (after " +
                                    QString::number(reconnectAttempts) + " attempts).");
                        reconnectAttempts = 0;
                    }
                }
                else {
                    timestamp = monotonicTimeMs();
                    reconnectAttempts = 0;
                }
            }
            else
#endif
            if (m_connectionType != "videoFile") {
                // Try to get frame from camera
                if (!cam->grab()) {
                    // Grab failed
                    status = false;
                    sendMessage("Warning: " + m_deviceName + " grab frame failed. Attempting to reconnect.");
                    if (cam->isOpened()) {
                        qDebug() << "Grab failed: Releasing cam" << m_cameraID;
                        cam->release();
                        qDebug() << "Released cam" << m_cameraID;
                    }
                    QThread::msleep(1000);

                    if (attemptReconnect()) {
                        // TODO: add some timeout here
                        sendMessage("Warning: " + m_deviceName + " reconnected.");
                        qDebug() << "Reconnect to camera" << m_cameraID;
                    }
                }
                else {
                    // Grab successful
                    timestamp = monotonicTimeMs();
                    if (!cam->retrieve(frame)) {
                        // Retrieve failed
                        status = false;
                        sendMessage("Warning: " + m_deviceName + " retrieve frame failed. Attempting to reconnect.");
                        if (cam->isOpened()) {
                            qDebug() << "Retieve failed: Releasing cam" << m_cameraID;
                            cam->release();
                            qDebug() << "Released cam" << m_cameraID;
                        }
                        QThread::msleep(1000);

                        if (attemptReconnect()) {
                            // TODO: add some timeout here
                            sendMessage("Warning: " + m_deviceName + " reconnected.");
                            qDebug() << "Reconnect to camera" << m_cameraID;
                        }
                    }
                }
            }
            else if (m_connectionType == "videoFile") {
                QThread::msleep(1000.0/m_playbackFPS);
                timestamp = monotonicTimeMs();
                if (!cam->read(frame)) {
                    // Try next file before fully giving up. End playback with
                    // a break, not a return: break falls through to the
                    // cam->release() after the loop, a return leaks the handle.
                    m_playbackFileIndex++;
                    qDebug() << "FILE INDEX" << m_playbackFileIndex;
                    fileName = m_playbackFolderPath + "/" + m_playbackFilePrefix + QString::number(m_playbackFileIndex) + ".avi";
                    cam->release();
                    if (!cam->open(fileName.toStdString()) || !cam->read(frame)) {
                        sendMessage(m_deviceName + " playback reached the end of the recording.");
                        m_isStreaming = false;
                        break;
                    }
                }
            }
            if (status) {
                // Grab and retrieve successful. Reserve a buffer slot BEFORE
                // writing anything into it: when the buffer is full,
                // idx%frameBufferSize is the oldest unconsumed slot, which
                // DataSaver may be reading right now - writing first corrupts
                // the frame being saved. Acquiring first also skips the
                // per-frame control reads for frames we are about to drop.
                if (!freeFrames->tryAcquire()) {
                    // No free slot: this frame is thrown away
                    if (freeFrames->available() == 0) {
                        // Buffers are full!
                        sendMessage("Error: " + m_deviceName + " frame buffer is full. Frames will be lost!");
                        QThread::msleep(100);
                    }
                }
                else {
                    const int bufIdx = idx % frameBufferSize;
                    timeStampBuffer[bufIdx] = timestamp;
                    if (m_isColor) {
                        frame.copyTo(frameBuffer[bufIdx]);
                    }
                    else {
                        cv::cvtColor(frame, frameBuffer[bufIdx], cv::COLOR_BGR2GRAY);
                    }

                    // Control-property reads below go through cv::VideoCapture;
                    // in the AVF path there is none (cam stays nullptr) and
                    // AVFoundation exposes no UVC controls anyway.
                    if (m_trackExtTrigger && cam != nullptr) {
                        if (extTriggerLast == -1) {
                            // first time grabbing trigger state.
                            extTriggerLast = cam->get(cv::CAP_PROP_GAMMA);
                        }
                        else {
                            extTrigger = cam->get(cv::CAP_PROP_GAMMA);
                            if (extTriggerLast != extTrigger) {
                                // State change
                                if (extTriggerLast == 0) {
                                    // Went from 0 to 1
                                    emit extTriggered(true);
                                }
                                else {
                                    // Went from 1 to 0
                                    emit extTriggered(false);
                                }
                            }
                            extTriggerLast = extTrigger;
                        }
                    }

                    if (m_headOrientationStreamState && cam != nullptr) {
                        // BNO output is a unit quaternion after 2^14 division
                        MiniscopeProtocol::unpackBnoQuaternion(
                            static_cast<qint16>(cam->get(cv::CAP_PROP_SATURATION)),
                            static_cast<qint16>(cam->get(cv::CAP_PROP_HUE)),
                            static_cast<qint16>(cam->get(cv::CAP_PROP_GAIN)),
                            static_cast<qint16>(cam->get(cv::CAP_PROP_BRIGHTNESS)),
                            &bnoBuffer[bufIdx*5]);
                    }
                    if (daqFrameNum != nullptr && cam != nullptr) {
                        *daqFrameNum = cam->get(cv::CAP_PROP_CONTRAST) - daqFrameNumOffset;
                        if (*m_acqFrameNum == 0) // Used to initially sync daqFrameNum with acqFrameNum
                            daqFrameNumOffset = *daqFrameNum - 1;
                    }
                    if (daqFrameNumBuffer != nullptr)
                        daqFrameNumBuffer[bufIdx] = (daqFrameNum != nullptr) ? qint64(daqFrameNum->loadRelaxed()) : qint64(-1);

                    m_acqFrameNum->operator++();
                    idx++;
                    emit newFrameAvailable(m_deviceName, *m_acqFrameNum);
                    usedFrames->release();
                }
            }
            // Get any new events
            QCoreApplication::processEvents(); // Is there a better way to do this. This is against best practices
            if (!m_commandQueue.isEmpty())
                sendCommands(); // Send last of each control property events that arrived on this processEvent() call then removes it from queue
        }
        if (cam != nullptr)
            cam->release();
#ifdef Q_OS_MACOS
        m_grabber.release();
#endif
    }
    else {
        sendMessage("Error: Could not connect to video stream " + QString::number(m_cameraID));
        qDebug() << "Camera " << m_cameraID << " failed to open.";
    }
}

void VideoStreamOCV::stopSteam()
{
    m_stopStreaming = true;
}

void VideoStreamOCV::setPropertyI2C(long preambleKey, QVector<quint8> packet)
{
    // A newer packet for the same preamble key replaces the unsent older one.
    m_commandQueue.set(preambleKey, packet);
}

void VideoStreamOCV::setExtTriggerTrackingState(bool state)
{
    m_trackExtTrigger = state;
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

// The UVC selector each packed word travels on, as this backend's OpenCV
// property. OpenCV property IDs and UVC PU selectors are different vocabularies
// for the same controls; this is the single translation point.
static int capPropForSelector(quint8 selector)
{
    switch (selector) {
    case MiniscopeProtocol::SEL_CONTRAST:  return cv::CAP_PROP_CONTRAST;
    case MiniscopeProtocol::SEL_GAMMA:     return cv::CAP_PROP_GAMMA;
    case MiniscopeProtocol::SEL_SHARPNESS: return cv::CAP_PROP_SHARPNESS;
    default:                               return -1;
    }
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
    if (!m_commandQueue.flush([this](quint8 sel, quint16 word) {
            return camSetProperty(cam, capPropForSelector(sel), word);
        }))
        qDebug() << "Send setting failed";
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
    if (m_connectionType == "DSHOW") {
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
    sendSerdesModeCommands(m_pixelClock);
    cam->set(cv::CAP_PROP_FRAME_WIDTH, m_expectedWidth);
    cam->set(cv::CAP_PROP_FRAME_HEIGHT, m_expectedHeight);
    QThread::msleep(500);
    requestInitCommands();
    return true;
}
