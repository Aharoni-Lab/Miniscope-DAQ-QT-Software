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

// For Pose Track Display
#define NUM_MAX_POSE_TRACES   16
#define TRACE_DISPLAY_BUFFER_SIZE   256

class BehaviorTracker : public QObject
{
    Q_OBJECT
public:
    explicit BehaviorTracker(QObject *parent = nullptr, QJsonObject userConfig = QJsonObject(), qint64 softwareStartTime = 0);
    void parseUserConfigTracker();
    void loadCamCalibration(QString name);
    void setBehaviorCamBufferParameters(QString name, qint64* timeBuf, cv::Mat* frameBuf, int bufSize, QAtomicInt* acqFrameNum);

    void cameraCalibration();
    void createView();
    void connectSnS();
    void setUpDLCLive();
    void startThread();



    QVector<float>* getPoseBufferPointer() { return poseBuffer;}
    int* getPoseFrameNumBufferPointer() {return poseFrameNumBuffer; }
    int getPoseBufferSize() { return POSE_BUFFER_SIZE; }
    QSemaphore* getFreePosePointer() { return freePoses; }
    QSemaphore* getUsedPosePointer() { return usedPoses; }


signals:
    void sendMessage(QString msg);
    void closeWorker();
    void addTraceDisplay(QString, float c[3], float, bool sameOffset, QAtomicInt*, QAtomicInt*, int , float*, float*);

public slots:
    void testSlot(QString msg) { qDebug() << msg; }
    void sendNewFrame();
    void startRunning(); // Slot gets called when thread starts
    void close();
    void handleAddNewTracePose(int poseIdx);

private:
    int initNumpy();
    BehaviorTrackerWorker *behavTrackWorker;
    QThread *workerThread;

    QString m_trackerType;
    int numberOfCameras;

    // Info from behavior cameras
    QMap<QString, cv::Mat*> frameBuffer;
    QMap<QString, qint64*> timeStampBuffer;
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
    float colors[20*3]; // TODO: Shouldn't be hardcoding this!

    // Tracking states
    bool m_trackingRunning;

    double m_pCutoffDisplay;
    QJsonObject m_btConfig;

    qint64 m_softwareStartTime;

    bool tracesSetup;
    // For Pose Trace Display
    int m_numTraces;
    int m_tracePoseIdx[NUM_MAX_POSE_TRACES];
    float m_traceColors[NUM_MAX_POSE_TRACES][3];
    bool sameOffsetAsPrevious[NUM_MAX_POSE_TRACES];
    QAtomicInt m_traceDisplayBufNum[NUM_MAX_POSE_TRACES];
    QAtomicInt m_traceNumDataInBuf[NUM_MAX_POSE_TRACES][2];
    float m_traceDisplayY[NUM_MAX_POSE_TRACES][2][TRACE_DISPLAY_BUFFER_SIZE];
    float m_traceDisplayT[NUM_MAX_POSE_TRACES][2][TRACE_DISPLAY_BUFFER_SIZE];
};

#endif // BEHAVIORTRACKER_H
