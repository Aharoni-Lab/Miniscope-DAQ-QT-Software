#include "videostreamocv.h"
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <QDebug>
#include <QAtomicInt>
#include <QCoreApplication>

VideoStreamOCV::VideoStreamOCV(QObject *parent) : QObject(parent)
{

}

void VideoStreamOCV::setCameraID(int cameraID)  {
    m_cameraID = cameraID;
}

void VideoStreamOCV::setBufferParameters(cv::Mat *buf, int bufferSize, QSemaphore *freeFramesS, QSemaphore *usedFramesS, QAtomicInt *acqFrameNum){
    buffer = buf;
    frameBufferSize = bufferSize;
    freeFrames = freeFramesS;
    usedFrames = usedFramesS;
    m_acqFrameNum = acqFrameNum;
}

void VideoStreamOCV::startStream()
{
    cv::Mat frame;
    cam = new cv::VideoCapture;
    qDebug() << "Trying to open cam.";
    cam->open(m_cameraID);
    qDebug() << "Cam Openned";
    int idx = 0;
    forever {
        if (cam->isOpened()) {
            QCoreApplication::processEvents(); // Is there a better way to do this. This is against best practices
            cam->grab();
            //            freeFrames->acquire();
            cam->retrieve(frame);
            buffer[idx%frameBufferSize] = frame;
            m_acqFrameNum->operator++();
            idx++;
            usedFrames->release();
        }
    }
}

void VideoStreamOCV::stopSteam()
{
    int x = 1;
}

void VideoStreamOCV::setProperty(QString type, double value)
{
    qDebug() << "IN SLOT!!!!! " << type << " is " << value;
}
