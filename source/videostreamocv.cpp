#include "videostreamocv.h"
#include "miniscopeprotocol.h"
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <QDebug>
#include <QAtomicInt>
#include <QCoreApplication>
#include <QMap>
#include <QVector>
#include <QDateTime>
#include <QThread>
#include <QtMath>

VideoStreamOCV::VideoStreamOCV(QObject *parent, int width, int height, double pixelClock) :
    VideoStreamBase(parent),
    m_deviceName(""),
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
    if (cam->isOpened())
        cam->release();
}

int VideoStreamOCV::connect2Camera(int cameraID) {
    int connectionState = 0;
    m_cameraID = cameraID;
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
    // We need to make sure the MODE of the SERDES is correct
    // This needs to be done before any other commands are sent over SERDES
    // Currently this is for the 913/914 TI SERES
    // TODO: Probably should move this somewhere else
    QVector<quint8> packet;
    if (m_pixelClock > 0 && connectionState != 0) {
        if (m_pixelClock <= 50) {
            // Set to 12bit low frequency in this case

            // DES
            packet.append(0xC0); // I2C Address
            packet.append(0x1F); // reg
            packet.append(0b00010000); // data
            setPropertyI2C(0,packet);

            // SER
            packet.clear();
            packet.append(0xB0); // I2C Address
            packet.append(0x05); // reg
            packet.append(0b00100000); // data
            setPropertyI2C(1,packet);
        }
        else {
            // Set to 10bit high frequency in this case

            // DES
            packet.clear();
            packet.append(0xC0); // I2C Address
            packet.append(0x1F); // reg
            packet.append(0b00010001); // data
            setPropertyI2C(0,packet);

            // SER
            packet.clear();
            packet.append(0xB0); // I2C Address
            packet.append(0x05); // reg
            packet.append(0b00100001); // data
            setPropertyI2C(1,packet);

        }
        sendCommands();
        QThread::msleep(500);

    }

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
                                         int bufferSize, QSemaphore *freeFramesS, QSemaphore *usedFramesS,
                                         QAtomicInt *acqFrameNum, QAtomicInt *daqFrameNumber){
    frameBuffer = frameBuf;
    timeStampBuffer = tsBuf;
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
    cv::Mat frame;

    m_stopStreaming = false;

    if (cam->isOpened()) {
        m_isStreaming = true;
        forever {

            if (m_stopStreaming == true) {
                m_isStreaming = false;
                break;
            }

            status = true;
            // Get new frame and handle disconnects
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
                    timeStampBuffer[idx%frameBufferSize] = QDateTime().currentMSecsSinceEpoch();
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
                timeStampBuffer[idx%frameBufferSize] = QDateTime().currentMSecsSinceEpoch();
                if (!cam->read(frame)) {
                    // Try next file before fully giving up
                    m_playbackFileIndex++;
//                    if (m_playbackFileIndex == 4)
//                        m_playbackFileIndex = 0;
                    qDebug() << "FILE INDEX" << m_playbackFileIndex;
                    fileName = m_playbackFolderPath + "/" + m_playbackFilePrefix + QString::number(m_playbackFileIndex) + ".avi";
                    cam->release();
                    if (cam->open(fileName.toStdString())) {
                        if (!cam->read(frame)) {
                            status = false;
                            return;
                        }
                    }
                    else
                        return;

                }
            }
            if (status) {
                // frame was grabbed
                // Grab and retieve successful

                if (m_isColor) {
                    frame.copyTo(frameBuffer[idx%frameBufferSize]);
                }
                else {
                    //                            frame = cv::repeat(frame,4,4);
                    cv::cvtColor(frame, frameBuffer[idx%frameBufferSize], cv::COLOR_BGR2GRAY);
                }
                // qDebug() << "Frame Number:" << *m_acqFrameNum - cam->get(cv::CAP_PROP_CONTRAST);

                if (m_trackExtTrigger) {
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

                if (m_headOrientationStreamState) {
                    // BNO output is a unit quaternion after 2^14 division
                    MiniscopeProtocol::unpackBnoQuaternion(
                        static_cast<qint16>(cam->get(cv::CAP_PROP_SATURATION)),
                        static_cast<qint16>(cam->get(cv::CAP_PROP_HUE)),
                        static_cast<qint16>(cam->get(cv::CAP_PROP_GAIN)),
                        static_cast<qint16>(cam->get(cv::CAP_PROP_BRIGHTNESS)),
                        &bnoBuffer[(idx%frameBufferSize)*5]);
                }
                if (daqFrameNum != nullptr) {
                    *daqFrameNum = cam->get(cv::CAP_PROP_CONTRAST) - daqFrameNumOffset;
                    // qDebug() << cam->get(cv::CAP_PROP_CONTRAST);// *daqFrameNum;
                    if (*m_acqFrameNum == 0) // Used to initially sync daqFrameNum with acqFrameNum
                        daqFrameNumOffset = *daqFrameNum - 1;
                }

                // Handle thread safe controls of buffer
                if(!freeFrames->tryAcquire()) {
                    // Failed to acquire free frame
                    // Will throw away this acquired frame
                    if (freeFrames->available() == 0) {
                        // Buffers are full!
                        sendMessage("Error: " + m_deviceName + " frame buffer is full. Frames will be lost!");
                        QThread::msleep(100);
                    }
                }
                else {
                    m_acqFrameNum->operator++();
                    // qDebug() << *m_acqFrameNum << *daqFrameNum;
                    idx++;
                    emit newFrameAvailable(m_deviceName, *m_acqFrameNum);
                    usedFrames->release();
                }

            }
            // Get any new events
            QCoreApplication::processEvents(); // Is there a better way to do this. This is against best practices
            if (!sendCommandQueue.isEmpty())
                sendCommands(); // Send last of each control property events that arrived on this processEvent() call then removes it from queue
        }
        cam->release();
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
    // add newEvent to the queue for sending new settings to camera
    // overwrites data of previous preamble event that has not been sent to camera yet
    if (!sendCommandQueue.contains(preambleKey))
        sendCommandQueueOrder.append(preambleKey);
    sendCommandQueue[preambleKey] = packet;
}

void VideoStreamOCV::setExtTriggerTrackingState(bool state)
{
    m_trackExtTrigger = state;
}

void VideoStreamOCV::startRecording()
{
    if (cam->isOpened()){
        cam->set(cv::CAP_PROP_SATURATION, 0x0001);
    }
}

void VideoStreamOCV::stopRecording()
{
    if (cam->isOpened()){
        cam->set(cv::CAP_PROP_SATURATION, 0x0000);
    }
}

void VideoStreamOCV::openCamPropsDialog()
{
    if (cam->isOpened()){
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

void VideoStreamOCV::sendCommands()
{
    long key;
    QVector<quint8> packet;
    while (!sendCommandQueueOrder.isEmpty()) {
        key = sendCommandQueueOrder.first();
        packet = sendCommandQueue[key];
        const auto cmd = MiniscopeProtocol::packI2CPacket(packet);
        if (cmd.valid) {
            // words[i] travels on kI2CWordSelectors[i]; through this backend
            // that means the matching DirectShow property (CONTRAST, GAMMA,
            // SHARPNESS - see miniscopeprotocol.h).
            bool success = camSetProperty(cam, cv::CAP_PROP_CONTRAST, cmd.words[0]);
            success = camSetProperty(cam, cv::CAP_PROP_GAMMA, cmd.words[1]) && success;
            success = camSetProperty(cam, cv::CAP_PROP_SHARPNESS, cmd.words[2]) && success;
            if (!success)
                qDebug() << "Send setting failed";
        }
        // Invalid (empty or > 6 byte) packets have no wire format and are dropped.
        sendCommandQueue.remove(key);
        sendCommandQueueOrder.removeFirst();
    }
}

bool VideoStreamOCV::attemptReconnect()
{
    // TODO: handle quitting nicely when stuck in this loop
    QVector<quint8> packet;
    if (m_connectionType == "DSHOW") {
        if (cam->open(m_cameraID, cv::CAP_DSHOW)) {

            if (m_pixelClock <= 50) {
                // Set to 12bit low frequency in this case

                // DES
                packet.append(0xC0); // I2C Address
                packet.append(0x1F); // reg
                packet.append(0b00010000); // data
                setPropertyI2C(0,packet);

                // SER
                packet.clear();
                packet.append(0xB0); // I2C Address
                packet.append(0x05); // reg
                packet.append(0b00100000); // data
                setPropertyI2C(1,packet);
            }
            else {
                // Set to 10bit high frequency in this case

                // DES
//                packet.clear();
                packet.append(0xC0); // I2C Address
                packet.append(0x1F); // reg
                packet.append(0b00010001); // data
                setPropertyI2C(0,packet);

                // SER
                packet.clear();
                packet.append(0xB0); // I2C Address
                packet.append(0x05); // reg
                packet.append(0b00100001); // data
                setPropertyI2C(1,packet);

            }
            sendCommands();
            QThread::msleep(500);

            cam->set(cv::CAP_PROP_FRAME_WIDTH, m_expectedWidth);
            cam->set(cv::CAP_PROP_FRAME_HEIGHT, m_expectedHeight);
            QThread::msleep(500);
            requestInitCommands();
            return true;
        }
    }
    else if (m_connectionType == "OTHER") {
        if (cam->open(m_cameraID)) {
            if (m_pixelClock <= 50) {
                // Set to 12bit low frequency in this case

                // DES
                packet.append(0xC0); // I2C Address
                packet.append(0x1F); // reg
                packet.append(0b00010000); // data
                setPropertyI2C(0,packet);

                // SER
                packet.clear();
                packet.append(0xB0); // I2C Address
                packet.append(0x05); // reg
                packet.append(0b00100000); // data
                setPropertyI2C(1,packet);
            }
            else {
                // Set to 10bit high frequency in this case

                // DES
//                packet.clear();
                packet.append(0xC0); // I2C Address
                packet.append(0x1F); // reg
                packet.append(0b00010001); // data
                setPropertyI2C(0,packet);

                // SER
                packet.clear();
                packet.append(0xB0); // I2C Address
                packet.append(0x05); // reg
                packet.append(0b00100001); // data
                setPropertyI2C(1,packet);

            }
            sendCommands();
            QThread::msleep(500);
            cam->set(cv::CAP_PROP_FRAME_WIDTH, m_expectedWidth);
            cam->set(cv::CAP_PROP_FRAME_HEIGHT, m_expectedHeight);
            QThread::msleep(500);
            requestInitCommands();
            return true;
        }
    }
    return false;
}
