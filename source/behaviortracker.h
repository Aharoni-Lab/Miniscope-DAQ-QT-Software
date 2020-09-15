#ifndef BEHAVIORTRACKER_H
#define BEHAVIORTRACKER_H

#include "newquickview.h"
#include "videodisplay.h"
#include "behaviortrackerworker.h"

#include <opencv2/opencv.hpp>

#include <QObject>
#include <QJsonObject>
#include <QAtomicInt>
#include <QMap>
#include <QString>
#include <QDebug>
#include <QQuickItem>
#include <QThread>
#include <QSemaphore>

#ifdef USE_PYTHON
 #undef slots
 #include <Python.h>
 #define slots
#endif

#define POSE_BUFFER_SIZE    128
// TODO: Move a bunch of stuff into structs across the whole project

class BehaviorTracker : public QObject
{
    Q_OBJECT
public:
    explicit BehaviorTracker(QObject *parent = nullptr, QJsonObject userConfig = QJsonObject());
    void parseUserConfigTracker();
    void loadCamCalibration(QString name);
    void setBehaviorCamBufferParameters(QString name, cv::Mat* frameBuf, int bufSize, QAtomicInt* acqFrameNum);

    void cameraCalibration();
    void createView();
    void connectSnS();
    void setUpDLCLive();
    void startThread();


signals:
    void sendMessage(QString msg);
    void closeWorker();

public slots:
    void testSlot(QString msg) { qDebug() << msg; }
    void sendNewFrame();
    void startRunning(); // Slot gets called when thread starts
    void close();

private:
    int initNumpy();
    BehaviorTrackerWorker *behavTrackWorker;
    QThread *workerThread;

    QString m_trackerType;
    int numberOfCameras;
    // Info from behavior cameras
    QMap<QString, cv::Mat*> frameBuffer;
    QMap<QString, QAtomicInt*> m_acqFrameNum;
    QMap<QString, int> bufferSize;

    QMap<QString, cv::Mat> currentFrame;
    QMap<QString, int> currentFrameNumberProcessed;
    QJsonObject m_userConfig;

    // For holding pose data
    QSemaphore *freePoses;
    QSemaphore *usedPoses;
    QVector<float> poseBuffer[POSE_BUFFER_SIZE];
    int poseFrameNumBuffer[POSE_BUFFER_SIZE];
    QAtomicInt *m_btPoseCount;
    // TODO: THink about where to generate timestamps for pose?

    int m_previousBtPoseFrameNum;

    // For GUI
    NewQuickView *view;
    QObject *rootObject;
    VideoDisplay *vidDisplay;

    // Tracking states
    bool m_trackingRunning;
};

#endif // BEHAVIORTRACKER_H
