#include "behaviortracker.h"
#include "newquickview.h"
#include "behaviortrackerworker.h"

#include <opencv2/opencv.hpp>

#include <QJsonObject>
#include <QDebug>
#include <QAtomicInt>
#include <QObject>
#include <QGuiApplication>
#include <QScreen>
#include <QQuickItem>
#include <QThread>

#ifdef USE_PYTHON
 #undef slots
 #include <Python.h>
 #include <numpy/arrayobject.h>
 #define slots
#endif

BehaviorTracker::BehaviorTracker(QObject *parent, QJsonObject userConfig) :
    QObject(parent),
    numberOfCameras(0),
    m_trackingRunning(false),
    m_btPoseCount(new QAtomicInt(0)),
    m_previousBtPoseFrameNum(0),
    usedPoses(new QSemaphore()),
    freePoses(new QSemaphore())
{

    freePoses->release(POSE_BUFFER_SIZE);

    m_userConfig = userConfig;
    parseUserConfigTracker();


    behavTrackWorker = new BehaviorTrackerWorker(NULL, m_userConfig["behaviorTracker"].toObject());
    behavTrackWorker->setPoseBufferParameters(poseBuffer, poseFrameNumBuffer, POSE_BUFFER_SIZE, m_btPoseCount, freePoses, usedPoses);
    workerThread = new QThread();

//    createView();
}

int BehaviorTracker::initNumpy()
{
    import_array1(-1);
}

void BehaviorTracker::parseUserConfigTracker()
{
//    QJsonObject jTracker = m_userConfig["behaviorTracker"].toObject();
//    m_trackerType = jTracker["type"].toString("None");

}

void BehaviorTracker::setBehaviorCamBufferParameters(QString name, cv::Mat *frameBuf, int bufSize, QAtomicInt *acqFrameNum)
{
    frameBuffer[name] = frameBuf;
    bufferSize[name] = bufSize;
    m_acqFrameNum[name] = acqFrameNum;

    currentFrameNumberProcessed[name] = 0;
    numberOfCameras++;

    // Set values/pointers for worker too
    behavTrackWorker->setParameters(name, frameBuf, bufSize, acqFrameNum);

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

void BehaviorTracker::setUpDLCLive()
{


}

void BehaviorTracker::startThread()
{
    behavTrackWorker->moveToThread(workerThread);

    QObject::connect(workerThread, SIGNAL (started()), behavTrackWorker, SLOT (startRunning()));
    QObject::connect(this, SIGNAL( closeWorker()), behavTrackWorker, SLOT (close()));
    QObject::connect(behavTrackWorker, &BehaviorTrackerWorker::sendMessage, this, &BehaviorTracker::sendMessage);

    // TODO: setup start connections

    workerThread->start();
}

void BehaviorTracker::startRunning()
{

}
void BehaviorTracker::close()
{
    emit closeWorker();
//    view->close();
}
