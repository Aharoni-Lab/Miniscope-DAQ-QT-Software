#ifndef BEHAVIORTRACKER_H
#define BEHAVIORTRACKER_H

#include "newquickview.h"
#include "videodisplay.h"
#include "behaviortrackerworker.h"

#include <opencv2/opencv.hpp>

#include <QtQuick/QQuickItem>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QOpenGLTexture>
#include <QtGui/QOpenGLBuffer>
#include <QtGui/QOpenGLFramebufferObject>

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
#define NUM_MAX_POSE_TRACES   40
#define TRACE_DISPLAY_BUFFER_SIZE   256

// For overlay plotting
//#define NUM_PAST_FRAMES_OVERLAY     10

typedef struct OverlayData{
    float position[3] ;
    float color;
    float index ;
    float pValue;
    int poseIdx;

} overlayData_t;

class TrackerDisplayRenderer : public QObject, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    TrackerDisplayRenderer(QObject *parent = nullptr, QSize displayWindowSize = QSize());
    ~TrackerDisplayRenderer();
    void initPrograms();

    void setViewportSize(const QSize &size) { m_viewportSize = size; }
    void setWindow(QQuickWindow *window) { m_window = window; }

    void setDisplayImage(QImage image) {m_displayImage = image.copy();  m_newImage = true;}
    void setDisplayOcc(QImage image) { m_displayOcc = image.copy(); m_newOccupancy = true; }
    void drawImage();
    void draw2DHist();
    void drawTrackerOverlay(QString type);


    bool m_newImage;
    bool m_newOccupancy;
    bool m_showOcc;
    bool overlayEnabled;
    bool overlaySkeletonEnabled;
    QString overlayType;
    int occMax;
    float occPlotBox[4];
    QVector<overlayData_t> overlayData;
    QVector<overlayData_t> overlaySkeletonData;
    float pValCut;
    double poseMarkerSize;

public slots:
    void paint();

private:
    qreal m_t;
    QImage m_displayImage;
    QImage m_displayOcc;

    QSize m_viewportSize;
    QOpenGLTexture *m_textureImage;
    QOpenGLTexture *m_texture2DHist;
    QQuickWindow *m_window;

    QOpenGLShaderProgram *m_programImage;
    QOpenGLShaderProgram *m_programOccupancy;

    QOpenGLShaderProgram *m_programTrackingOverlay;
    QOpenGLBuffer overlayDataVOB;
    QOpenGLBuffer overlaySkeletonVOB;
};

class TrackerDisplay : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(qreal t READ t WRITE setT NOTIFY tChanged)
public:
    TrackerDisplay();


    qreal t() const { return m_t; }
    void setT(qreal t);
    void setDisplayImage(QImage image);
    void setDisplayOcc(QImage image);
    void setOverlayData(QVector<overlayData_t> data, QString type);
    void setSkeletonData(QVector<overlayData_t> data);
    void setOccMax(int value) { m_renderer->occMax = value;}
    void setShowOccState(bool state);
    void setOverlayShowState(bool state);
    void setOverlaySkeletonShowState(bool state);
    void setPValueCutOff(float value) {m_pValCut = value; }
    void setPoseMarkerSize(double size) { m_poseMarkerSize = size; }

    Q_INVOKABLE void occRectChanged(float x, float y, float w, float h);

signals:
    void tChanged();

public slots:
    void sync();
    void cleanup();

private slots:
    void handleWindowChanged(QQuickWindow *win);

private:
    qreal m_t;
    float m_pValCut;
    bool m_showOcc;
    bool m_overlayEnabled;
    bool m_overlaySkeletonEnabled;
    TrackerDisplayRenderer* m_renderer;
    double m_poseMarkerSize;
//    float m_occPlotBox[4];

};
class BehaviorTracker : public QObject
{
    Q_OBJECT
public:
    explicit BehaviorTracker(QObject *parent = nullptr, QJsonObject userConfig = QJsonObject(), qint64 softwareStartTime = 0);
    void parseUserConfigTracker();
    void loadCamCalibration(QString name);
    void setBehaviorCamBufferParameters(QString name, qint64* timeBuf, cv::Mat* frameBuf, int bufSize, QAtomicInt* acqFrameNum);
    void setupDisplayTraces();

    void cameraCalibration();
    void createView(QSize resolution);
    void connectSnS();
    void setUpDLCLive();
    void startThread();
    void makeRibbon();



    QVector<float>* getPoseBufferPointer() { return poseBuffer;}
    int* getPoseFrameNumBufferPointer() {return poseFrameNumBuffer; }
    int getPoseBufferSize() { return POSE_BUFFER_SIZE; }
    QSemaphore* getFreePosePointer() { return freePoses; }
    QSemaphore* getUsedPosePointer() { return usedPoses; }


signals:
    void sendMessage(QString msg);
    void closeWorker();
    void addTraceDisplay(QString, float c[3], float, QString, bool sameOffset, QAtomicInt*, QAtomicInt*, int , float*, float*);

public slots:
    void testSlot(QString msg) { qDebug() << msg; }
    void sendNewFrame();
    void startRunning(); // Slot gets called when thread starts
    void close();
    void handleAddNewTracePose(int poseIdx, QString type, bool sameOffset);

private:
    int initNumpy();
    BehaviorTrackerWorker *behavTrackWorker;
    QThread *workerThread;

    QString m_trackerType;
    int numberOfCameras;
    QSize m_camResolution;

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
    TrackerDisplay *trackerDisplay;
    float colors[NUM_MAX_POSE_TRACES][3]; // TODO: Shouldn't be hardcoding this!

    // Tracking states
    bool m_trackingRunning;

    double m_pCutoffDisplay;
    QJsonObject m_btConfig;

    qint64 m_softwareStartTime;

    bool tracesSetup;
    // For Pose Trace Display
    int m_numTraces;
    int m_tracePoseIdx[NUM_MAX_POSE_TRACES];
    float m_traceWindowLength[NUM_MAX_POSE_TRACES];
    int m_tracePoseType[NUM_MAX_POSE_TRACES];
    float m_traceColors[NUM_MAX_POSE_TRACES][3];
    bool sameOffsetAsPrevious[NUM_MAX_POSE_TRACES];
    QAtomicInt m_traceDisplayBufNum[NUM_MAX_POSE_TRACES];
    QAtomicInt m_traceNumDataInBuf[NUM_MAX_POSE_TRACES][2];
    float m_traceDisplayY[NUM_MAX_POSE_TRACES][2][TRACE_DISPLAY_BUFFER_SIZE];
    float m_traceDisplayT[NUM_MAX_POSE_TRACES][2][TRACE_DISPLAY_BUFFER_SIZE];

    // For Occupancy Plotting
    cv::Mat* m_occupancy;
    bool m_plotOcc;
    int m_occNumBinsX;
    int m_occNumBinsY;
    int m_occMax;
    QVector<int> m_poseIdxUsed;

    // For tracker Overlay plotting
    QVector<overlayData_t> overlayPose;
    QVector<overlayData_t> overlayLine;
    QVector<overlayData_t> overlayRibbon;
    QVector<overlayData_t> overlaySkeleton;
    int m_numPose;
    QString m_overlayType;
    int m_overlayNumPoses;
    bool m_poseOverlayEnabled;
    bool m_poseOverlaySkeletonEnabled;
    double m_poseMarkerSize;

};

#endif // BEHAVIORTRACKER_H
