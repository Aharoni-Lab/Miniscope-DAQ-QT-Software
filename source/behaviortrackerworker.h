#ifndef BEHAVIORTRACKERWORKER_H
#define BEHAVIORTRACKERWORKER_H

#include <QObject>
#include <QAtomicInt>
#include <QVector>
#include <QMap>
#include <QDebug>
#include <QString>
#include <QJsonObject>
#include <QSemaphore>

#include <opencv2/opencv.hpp>

#ifdef USE_PYTHON
 #undef slots
 #include <Python.h>
 #define slots
#endif

class BehaviorTrackerWorker : public QObject
{
    Q_OBJECT
public:
    explicit BehaviorTrackerWorker(QObject *parent = nullptr, QJsonObject behavTrackerConfig = QJsonObject());
    void initPython();
    int initNumpy();
    void setUpDLCLive();
    QVector<float> getDLCLivePose(cv::Mat frame);
    void setParameters(QString name, cv::Mat *frameBuf, int bufSize, QAtomicInt *acqFrameNum);
    void setPoseBufferParameters(QVector<float> *poseBuf, int *poseFrameNumBuf, int poseBufSize, QAtomicInt *btPoseFrameNum, QSemaphore *free, QSemaphore *used);
    void getColors();

signals:
    void sendMessage(QString msg);

public slots:
    void startRunning(); // Gets called when thread starts
//    void stopRunning();
    void close();

private:
    QJsonObject m_btConfig;

    bool m_trackingRunning;
    bool m_DLCInitInfDone;

    // Info from behavior cameras
    QMap<QString, cv::Mat*> frameBuffer;
    QMap<QString, QAtomicInt*> m_acqFrameNum;
    QMap<QString, int> bufferSize;

    QMap<QString, cv::Mat> currentFrame;
    QMap<QString, int> currentFrameNumberProcessed;
    int numberOfCameras;

    // For holding pose data
    int poseBufferSize;
    QSemaphore *freePoses;
    QSemaphore *usedPoses;
    QVector<float> *poseBuffer;
    int *poseFrameNumBuffer;
    QAtomicInt *m_btPoseCount;
    float *colors;

    // For DLC python class
    PyObject *pInstance;
    PyObject *pClass;
    PyObject *pModule;
    PyObject *pDict;
    PyObject *pArgs;
    PyObject *pValue;

    bool m_PythonInitialized;
    int m_PythonError;
};

#endif // BEHAVIORTRACKERWORKER_H
