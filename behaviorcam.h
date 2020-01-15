#ifndef BEHAVIORCAM_H
#define BEHAVIORCAM_H

#include <QObject>
#include <QThread>
#include <QSemaphore>
#include <QTimer>
#include <QAtomicInt>
#include <QJsonObject>
#include <QQuickView>
#include <QMap>
#include <QVector>
#include <QQuickItem>

#include "videostreamocv.h"
#include "videodisplay.h"
#include "newquickview.h"
#include <opencv2/opencv.hpp>

#define FRAME_BUFFER_SIZE   128

class BehaviorCam : public QObject
{
    Q_OBJECT
public:
    explicit BehaviorCam(QObject *parent = nullptr, QJsonObject ucBehavCam = QJsonObject());
    void createView();
    void connectSnS();
    void parseUserConfigBehavCam();
//    void sendInitCommands();
    cv::Mat* getFrameBufferPointer(){return frameBuffer;}
    qint64* getTimeStampBufferPointer(){return timeStampBuffer;}
    int getBufferSize() {return FRAME_BUFFER_SIZE;}
    QSemaphore* getFreeFramesPointer(){return freeFrames;}
    QSemaphore* getUsedFramesPointer(){return usedFrames;}
    QAtomicInt* getAcqFrameNumPointer(){return m_acqFrameNum;}
//    QAtomicInt* getDAQFrameNumPointer() { return m_daqFrameNum; }
    QString getDeviceName() {return m_deviceName;}



signals:
    // TODO: setup signals to configure camera in thread
//    void setPropertyI2C(long preambleKey, QVector<quint8> packet);
    void onPropertyChanged(QString devieName, QString propName, double propValue);
    void sendMessage(QString msg);
    void takeScreenShot(QString type);
    void newFrameAvailable(int frameNum);

public slots:
    void sendNewFrame();
    void testSlot(QString, double);
    void handlePropCangedSignal(QString type, double displayValue, double i2cValue);
    void handleTakeScreenShotSignal();

private:
    void getBehavCamConfig(QString deviceType);
    void configureBehavCamControls();
//    QVector<QMap<QString, int>> parseSendCommand(QJsonArray sendCommand);
    int processString2Int(QString s);

    NewQuickView *view;
    VideoStreamOCV *behavCamStream;
    QThread *videoStreamThread;
    cv::Mat frameBuffer[FRAME_BUFFER_SIZE];
    cv::Mat tempFrame;
    qint64 timeStampBuffer[FRAME_BUFFER_SIZE];
    QSemaphore *freeFrames;
    QSemaphore *usedFrames;
    QObject *rootObject;
    VideoDisplay *vidDisplay;
    QTimer *timer;
    int m_previousDisplayFrameNum;
    QAtomicInt *m_acqFrameNum;
//    QAtomicInt *m_daqFrameNum;

//    QAtomicInt *m_DAQTimeStamp;

    // User Config parameters
    QJsonObject m_ucBehavCam;
    int m_deviceID;
    QString m_deviceName;
    QString m_deviceType;


    QJsonObject m_cBehavCam; // Consider renaming to not confuse with ucMiniscopes
//    QMap<QString,QVector<QMap<QString, int>>> m_controlSendCommand;
//    QMap<QString, int> m_sendCommand;

    bool m_streamHeadOrientationState;
};

#endif // BEHAVIORCAM_H
