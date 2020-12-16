#include "behaviortracker.h"
#include "newquickview.h"
#include "videodisplay.h"
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

BehaviorTracker::BehaviorTracker(QObject *parent, QJsonObject userConfig, qint64 softwareStartTime) :
    QObject(parent),
    numberOfCameras(0),
    m_trackingRunning(false),
    m_btPoseCount(new QAtomicInt(0)),
    m_previousBtPoseFrameNum(0),
    usedPoses(new QSemaphore()),
    freePoses(new QSemaphore()),
    m_pCutoffDisplay(0),
    m_softwareStartTime(softwareStartTime)
{

    freePoses->release(POSE_BUFFER_SIZE);

    m_userConfig = userConfig;
    parseUserConfigTracker();


    behavTrackWorker = new BehaviorTrackerWorker(NULL, m_userConfig["behaviorTracker"].toObject());
    behavTrackWorker->setPoseBufferParameters(poseBuffer, poseFrameNumBuffer, POSE_BUFFER_SIZE, m_btPoseCount, freePoses, usedPoses, colors);
    workerThread = new QThread();

}

int BehaviorTracker::initNumpy()
{
    import_array1(-1);
}

void BehaviorTracker::parseUserConfigTracker()
{
    m_btConfig = m_userConfig["behaviorTracker"].toObject();
//    QJsonObject jTracker = m_userConfig["behaviorTracker"].toObject();
//    m_trackerType = jTracker["type"].toString("None");
    m_pCutoffDisplay = m_btConfig["pCutoffDisplay"].toDouble(0);

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
    QJsonObject btConfig = m_userConfig["behaviorTracker"].toObject();

    qmlRegisterType<VideoDisplay>("VideoDisplay", 1, 0, "VideoDisplay");
    const QUrl url("qrc:/behaviorTracker.qml");
    view = new NewQuickView(url);

    // TODO: Probably should grab this from the behavior cam size...
    view->setWidth(btConfig["windowWidth"].toInt(640) * btConfig["windowScale"].toDouble(1));
    view->setHeight(btConfig["windowHeight"].toInt(480) * btConfig["windowScale"].toDouble(1));

    view->setTitle("Behavior Tracker");
    view->setX(btConfig["windowX"].toInt(1));
    view->setY(btConfig["windowY"].toInt(1));

#ifdef Q_OS_WINDOWS
    view->setFlags(Qt::Window | Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint);
#endif
    view->show();

    rootObject = view->rootObject();
    vidDisplay = rootObject->findChild<VideoDisplay*>("vD");
    QObject::connect(vidDisplay->window(), &QQuickWindow::beforeRendering, this, &BehaviorTracker::sendNewFrame);
}

void BehaviorTracker::connectSnS()
{

}

void BehaviorTracker::setUpDLCLive()
{


}

void BehaviorTracker::startThread()
{
    //TODO: Stop thread and working if missing path to py env.
    if (m_userConfig["behaviorTracker"].toObject().contains("pyEnvPath")) {
        sendMessage("Using \"" + m_userConfig["behaviorTracker"].toObject()["pyEnvPath"].toString() + "\" as Python Environment.");

        behavTrackWorker->moveToThread(workerThread);

        QObject::connect(workerThread, SIGNAL (started()), behavTrackWorker, SLOT (startRunning()));
        QObject::connect(this, SIGNAL( closeWorker()), behavTrackWorker, SLOT (close()));
        QObject::connect(behavTrackWorker, &BehaviorTrackerWorker::sendMessage, this, &BehaviorTracker::sendMessage);

        workerThread->start();
    }
    else
        // Py environment path missing
        sendMessage("Error: Path to Python Environment (\"pyEnvPath:\") missing from user config file!");


}

void BehaviorTracker::sendNewFrame()
{
    // TODO: currently writen to handle only 1 camera
    int poseNum = *m_btPoseCount;
    if (poseNum > m_previousBtPoseFrameNum) {
        m_previousBtPoseFrameNum = poseNum;
        cv::Mat cvFrame;
        QImage qFrame;
        int frameNum = poseFrameNumBuffer[(poseNum - 1) % POSE_BUFFER_SIZE];
        int frameIdx = frameNum % bufferSize.first();
        if (frameBuffer.first()[frameIdx].channels() == 1) {
            cv::cvtColor(frameBuffer.first()[frameIdx], cvFrame, cv::COLOR_GRAY2BGR);
        }
        else {
            cvFrame = frameBuffer.first()[frameIdx].clone();

        }
        // TODO: Shouldn't hardcore pose vector size/shape
        QVector<float> pose = poseBuffer[(poseNum - 1) % POSE_BUFFER_SIZE];
        float w, h, l;
        for (int i = 0; i < 20; i++) {
//            if ((i < 5) || ((i >=8) && (i <= 15))) {
            w = pose[i];
            h = pose[i + 20];
            l = pose[i + 40];
            if (l > m_pCutoffDisplay)
                cv::circle(cvFrame, cv::Point(w,h),3,cv::Scalar(colors[i*3]*1.5,colors[i*3+1]*1.5,colors[i*3+2]*1.5),cv::FILLED);
//        }
        }
        qFrame = QImage(cvFrame.data, cvFrame.cols, cvFrame.rows, cvFrame.step, QImage::Format_RGB888);
        vidDisplay->setDisplayFrame(qFrame);
    }
    // TODO: Move QSemaphores to dataSaver
//    usedPoses->tryAcquire();
//    freePoses->release();


}

void BehaviorTracker::startRunning()
{

}
void BehaviorTracker::close()
{
    emit closeWorker();
//    view->close();
}
