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

VideoStreamOCV::VideoStreamOCV(QObject *parent) :
    QObject(parent),
    m_stopStreaming(false),
    m_streamHeadOrientationState(false)
{

}

VideoStreamOCV::~VideoStreamOCV() {
    qDebug() << "Closing video stream";
    if (cam->isOpened())
        cam->release();
}

int VideoStreamOCV::connect2Camera(int cameraID) {
    m_cameraID = cameraID;
    cam = new cv::VideoCapture;
    cam->open(m_cameraID);
    qDebug() <<  "Camera capture backend is" << QString::fromStdString (cam->getBackendName());
//    cam->set(cv::CAP_PROP_SETTINGS, 0);
    if (cam->isOpened())
        return 1;
    else
        return 0;

}

void VideoStreamOCV::setBufferParameters(cv::Mat *frameBuf, qint64 *tsBuf, float *bnoBuf, int bufferSize, QSemaphore *freeFramesS, QSemaphore *usedFramesS, QAtomicInt *acqFrameNum){
    frameBuffer = frameBuf;
    timeStampBuffer = tsBuf;
    frameBufferSize = bufferSize;
    bnoBuffer = bnoBuf;
    freeFrames = freeFramesS;
    usedFrames = usedFramesS;
    m_acqFrameNum = acqFrameNum;

}

void VideoStreamOCV::startStream()
{
    int idx = 0;
    float heading, pitch, roll;
    cv::Mat frame;

    m_stopStreaming = false;

    if (cam->isOpened()) {
        m_isStreaming = true;
        forever {

            if (m_stopStreaming == true) {
                m_isStreaming = false;
                break;
            }
            if(freeFrames->tryAcquire(1,30)) {
                if (!cam->grab())
                    qDebug() << "Cam grab failed";
                else {
                    timeStampBuffer[idx%frameBufferSize] = QDateTime().currentMSecsSinceEpoch();
                    if (!cam->retrieve(frame)) {
                        qDebug() << "Cam retrieve failed";
                        qDebug() << "State of cam" << m_cameraID << "is" << cam->isOpened();
//                        qDebug() << frame.cols << "|" << frame.rows;
                        if (cam->isOpened()) {
                            qDebug() << "Releasing cam" << m_cameraID;
                            cam->release();
                            qDebug() << "Released cam" << m_cameraID;
                        }
                        cam->open(m_cameraID);
                        qDebug() << "Reconnect to camera" << m_cameraID;
                    }
                    else {
                        cv::cvtColor(frame, frameBuffer[idx%frameBufferSize], cv::COLOR_BGR2GRAY);
        //                frameBuffer[idx%frameBufferSize] = frame;
                        if (m_streamHeadOrientationState) {
                            heading = static_cast<qint16>(cam->get(cv::CAP_PROP_SATURATION))/16.0;
                            roll = static_cast<qint16>(cam->get(cv::CAP_PROP_HUE))/16.0;
                            pitch = static_cast<qint16>(cam->get(cv::CAP_PROP_GAIN))/16.0;
                            bnoBuffer[(idx%frameBufferSize)*3 + 0] = heading;
                            bnoBuffer[(idx%frameBufferSize)*3 + 1] = roll;
                            bnoBuffer[(idx%frameBufferSize)*3 + 2] = pitch;
                        }
                        m_acqFrameNum->operator++();
                        idx++;
                        usedFrames->release();



                    }
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
