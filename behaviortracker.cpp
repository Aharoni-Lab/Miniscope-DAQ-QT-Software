#include "behaviortracker.h"

#include <opencv2/opencv.hpp>

#include <QJsonObject>
#include <QDebug>
#include <QAtomicInt>
#include <QObject>
#include <QGuiApplication>
#include <QScreen>
#include <QQuickItem>

BehaviorTracker::BehaviorTracker(QObject *parent, QJsonObject userConfig) :
    QObject(parent),
    numberOfCameras(0)
{
    m_userConfig = userConfig;
    parseUserConfigTracker();

    createView();
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

    currentFrameNumberProcessed[name] = 0;
    numberOfCameras++;
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

void BehaviorTracker::createView()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect  screenGeometry = screen->geometry();
    int height = screenGeometry.height();
    int width = screenGeometry.width();

    const QUrl url(QStringLiteral("qrc:/behaviorTracker.qml"));
    view = new NewQuickView(url);

    view->setWidth(400);
    view->setHeight(200);
    view->setTitle("Behavior Tracker");
    view->setX(400);
    view->setY(50);
    view->show();

    rootObject = view->rootObject();
//    messageTextArea = rootObject->findChild<QQuickItem*>("messageTextArea");

    connectSnS();
}

void BehaviorTracker::connectSnS()
{

}

void BehaviorTracker::handleNewFrameAvailable(QString name, int frameNum)
{

}
