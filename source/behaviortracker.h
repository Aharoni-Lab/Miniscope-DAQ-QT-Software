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
#define NUM_MAX_POSE_TRACES   40
#define TRACE_DISPLAY_BUFFER_SIZE   256

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



    bool m_newImage;
    bool m_newOccupancy;
    bool m_showOcc;
    int occMax;
    float occPlotBox[4];

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
};

class TrackerDisplay : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(qreal t READ t WRITE setT NOTIFY tChanged)
public:
    TrackerDisplay();

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

    qreal t() const { return m_t; }
    void setT(qreal t);
    void setDisplayImage(QImage image);
    void setDisplayOcc(QImage image);
    void setOccMax(int value) { m_renderer->occMax = value;}
    void setShowOccState(bool state) {m_showOcc = state; }

    Q_INVOKABLE void occRectMoved(float x, float y);

signals:
    void tChanged();

public slots:
    void sync();
    void cleanup();

private slots:
    void handleWindowChanged(QQuickWindow *win);

private:
    qreal m_t;
    bool m_showOcc;
    TrackerDisplayRenderer* m_renderer;
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
};

#endif // BEHAVIORTRACKER_H
