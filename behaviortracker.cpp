#include "behaviortracker.h"

#include <opencv2/opencv.hpp>

#include <QJsonObject>
#include <QDebug>
#include <QAtomicInt>
#include <QObject>

BehaviorTracker::BehaviorTracker(QObject *parent, QJsonObject userConfig) :
    QObject(parent)
{
    m_userConfig = userConfig;
    parseUserConfigTracker();
}

void BehaviorTracker::parseUserConfigTracker()
{
    QJsonObject jTracker = m_userConfig["behaviorTracker"].toObject();
    m_trackerType = jTracker["type"].toString("None");

}

void BehaviorTracker::setBehaviorCamBufferParameters(QString name, cv::Mat *frameBuf, int bufSize, QAtomicInt *acqFrameNum)
{
    frameBuffer[name] = frameBuf;
    bufferSize[name] = bufSize;
    m_acqFrameNum[name] = acqFrameNum;
}

void BehaviorTracker::cameraCalibration()
{
    // calibrate differently for 1 and 2 cameras
    // for 1 camera need to define points on track and use prospective
    // for 2 cameras we can use stereo calibrate and find 3d point
    // ask user for specs of calibration grid (or just use a standard one
    // collect images in increments of a few seconds
    // display if calibration grid was detected
    // run calibration and save to file(s)
}

void BehaviorTracker::handleNewFrameAvailable(int frameNum)
{
    qDebug() << "Frame" << frameNum;
}
