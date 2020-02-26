#include "videostreamocv.h"
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

VideoStreamOCV::VideoStreamOCV(QObject *parent, int width, int height) :
    QObject(parent),
    m_deviceName(""),
    m_stopStreaming(false),
    m_streamHeadOrientationState(false),
    m_isColor(false),
    m_trackExtTrigger(false),
    m_expectedWidth(width),
    m_expectedHeight(height),
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
    // Try connecting using DShow backend
    if (cam->open(m_cameraID, cv::CAP_DSHOW)) {
        connectionState = 1;
        m_connectionType = "DSHOW";
    }
    else {
        // connecting again using defaulk backend
        if (cam->open(m_cameraID)) {
            connectionState = 2;
            m_connectionType = "OTHER";
        }
    }
//    qDebug() <<  "Camera capture backend is" << QString::fromStdString (cam->getBackendName());
    return connectionState;


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
    int idx = 0;
    int daqFrameNumOffset = 0;
//    float heading, pitch, roll;
    double norm;
    double w, x, y, z;
    double extTriggerLast = -1;
    double extTrigger;
    cv::Mat frame;

    m_stopStreaming = false;

    if (cam->isOpened()) {
        m_isStreaming = true;
        forever {

            if (m_stopStreaming == true) {
                m_isStreaming = false;
                break;
            }
            if (freeFrames->available() == 0) {
                // Buffers are full!
                sendMessage("Error: " + m_deviceName + " frame buffer is full. Frames will be lost!");
            }

            if(freeFrames->tryAcquire(1,100)) {
                if (!cam->grab()) {
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
                    timeStampBuffer[idx%frameBufferSize] = QDateTime().currentMSecsSinceEpoch();
                    if (!cam->retrieve(frame)) {
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
                    else {

                        // Let's make sure the frame acquired has the correct size. An openCV error seems to occur on cam reconnect due to a mismatch in size.
//                        if (frame.cols != m_expectedWidth || frame.rows != m_expectedHeight) {
//                            sendMessage("Warning: " + m_deviceName + " acquired frame has wrong size. [" + QString::number(frame.cols) + ", " + QString::number(frame.rows) + "]");
//                            qDebug() << "Wrong frame size for device" << m_cameraID;

//                            // This likely means the correct video stream crashed and openCV defaulted to a different video stream. So lets disconnect and try to reconnect to the correct one
//                            cam->release();
//                        }
//                        else {
                        if (true) {
                            if (m_isColor) {
                                frame.copyTo(frameBuffer[idx%frameBufferSize]);
                            }
                            else {
    //                            frame = cv::repeat(frame,4,4);
                                cv::cvtColor(frame, frameBuffer[idx%frameBufferSize], cv::COLOR_BGR2GRAY);
                            }
    //                        qDebug() << "Frame Number:" << *m_acqFrameNum - cam->get(cv::CAP_PROP_CONTRAST);

            //                frameBuffer[idx%frameBufferSize] = frame;
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
                            if (m_streamHeadOrientationState) {
                                // BNO output is a unit quaternion after 2^14 division
                                w = static_cast<qint16>(cam->get(cv::CAP_PROP_SATURATION));
                                x = static_cast<qint16>(cam->get(cv::CAP_PROP_HUE));
                                y = static_cast<qint16>(cam->get(cv::CAP_PROP_GAIN));
                                z = static_cast<qint16>(cam->get(cv::CAP_PROP_BRIGHTNESS));
                                norm = sqrt(w*w + x*x + y*y + z*z);
                                bnoBuffer[(idx%frameBufferSize)*4 + 0] = w/16384.0;
                                bnoBuffer[(idx%frameBufferSize)*4 + 1] = x/16384.0;
                                bnoBuffer[(idx%frameBufferSize)*4 + 2] = y/16384.0;
                                bnoBuffer[(idx%frameBufferSize)*4 + 3] = z/16384.0;
    //                            qDebug() << QString::number(static_cast<qint16>(cam->get(cv::CAP_PROP_SHARPNESS)),2) << norm << w << x << y << z ;
                            }
                            if (daqFrameNum != nullptr) {
                                *daqFrameNum = cam->get(cv::CAP_PROP_CONTRAST) - daqFrameNumOffset;
    //                            qDebug() << cam->get(cv::CAP_PROP_CONTRAST);// *daqFrameNum;
                                if (*m_acqFrameNum == 0) // Used to initially sync daqFrameNum with acqFrameNum
                                    daqFrameNumOffset = *daqFrameNum - 1;
                            }

                            m_acqFrameNum->operator++();
    //                        qDebug() << *m_acqFrameNum << *daqFrameNum;
                            idx++;
//                            usedFrames->release();

                            emit newFrameAvailable(m_deviceName, *m_acqFrameNum);
                        }
                    }
                }
                usedFrames->release();
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

void VideoStreamOCV::sendCommands()
{
//    QList<long> keys = sendCommandQueue.keys();
    bool success = false;
    long key;
    QVector<quint8> packet;
    quint64 tempPacket;
//    qDebug() << "New Loop";
//    qDebug() << "Queue length is " << sendCommandQueueOrder.length();
    while (!sendCommandQueueOrder.isEmpty()) {
        key = sendCommandQueueOrder.first();
        packet = sendCommandQueue[key];
        qDebug() << packet;
        if (packet.length() < 6){
            tempPacket = (quint64)packet[0]; // address
            tempPacket |= (((quint64)packet.length())&0xFF)<<8; // data length
            for (int j = 1; j < packet.length(); j++)
                tempPacket |= ((quint64)packet[j])<<(8*(j+1));
            qDebug() << "1-5: 0x" << QString::number(tempPacket,16);
//            cam->set(cv::CAP_PROP_GAMMA, tempPacket);
            success = cam->set(cv::CAP_PROP_CONTRAST, (tempPacket & 0x00000000FFFF));
            success = cam->set(cv::CAP_PROP_GAMMA, (tempPacket & 0x0000FFFF0000)>>16);
            success = cam->set(cv::CAP_PROP_SHARPNESS, (tempPacket & 0xFFFF00000000)>>32);
            if (!success)
                qDebug() << "Send setting failed";
            sendCommandQueue.remove(key);
            sendCommandQueueOrder.removeFirst();
        }
        else if (packet.length() == 6) {
            tempPacket = (quint64)packet[0] | 0x01; // address with bottom bit flipped to 1 to indicate a full 6 byte package
            for (int j = 1; j < packet.length(); j++)
                tempPacket |= ((quint64)packet[j])<<(8*(j));
            qDebug() << "6: 0x" << QString::number(tempPacket,16);

//            success = cam->set(cv::CAP_PROP_GAIN, 0x1122ff20);


            success = cam->set(cv::CAP_PROP_CONTRAST, (tempPacket & 0x00000000FFFF));
            success = cam->set(cv::CAP_PROP_GAMMA, (tempPacket & 0x0000FFFF0000)>>16);
            success = cam->set(cv::CAP_PROP_SHARPNESS, (tempPacket & 0xFFFF00000000)>>32);

            if (!success)
                qDebug() << "Send setting failed";


            sendCommandQueue.remove(key);
            sendCommandQueueOrder.removeFirst();
        }
        else {
            //TODO: Handle packets longer than 6 bytes
        }

    }

}

bool VideoStreamOCV::attemptReconnect()
{
    if (m_connectionType == "DSHOW") {
        if (cam->open(m_cameraID, cv::CAP_DSHOW))
            return true;
    }
    else if (m_connectionType == "OTHER") {
        if (cam->open(m_cameraID))
            return true;
    }
    return false;
}
