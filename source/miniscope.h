#ifndef MINISCOPE_H
#define MINISCOPE_H

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
#include <QVariant>

#include "videostreamocv.h"
#include "videodisplay.h"
#include "newquickview.h"
#include <opencv2/opencv.hpp>


#define PROTOCOL_I2C            -2
#define PROTOCOL_SPI            -3
#define SEND_COMMAND_VALUE_H    -5
#define SEND_COMMAND_VALUE_L    -6
#define SEND_COMMAND_VALUE      -6
#define SEND_COMMAND_VALUE_H16  -7
#define SEND_COMMAND_VALUE_H24  -8
#define SEND_COMMAND_VALUE2_H   -9
#define SEND_COMMAND_VALUE2_L   -10
#define SEND_COMMAND_ERROR      -20

#define FRAME_BUFFER_SIZE   128
#define BASELINE_FRAME_BUFFER_SIZE  128


class Miniscope : public QObject
{
    Q_OBJECT
public:
    explicit Miniscope(QObject *parent = nullptr, QJsonObject ucMiniscope = QJsonObject());
    void createView();
    void connectSnS();
    void defineDeviceAddrs();
    void parseUserConfigMiniscope();
    void sendInitCommands();
    QString getCompressionType();
    cv::Mat* getFrameBufferPointer(){return frameBuffer;}
    qint64* getTimeStampBufferPointer(){return timeStampBuffer;}
    float* getBNOBufferPointer() { return bnoBuffer; }
    int getBufferSize() {return FRAME_BUFFER_SIZE;}
    QSemaphore* getFreeFramesPointer(){return freeFrames;}
    QSemaphore* getUsedFramesPointer(){return usedFrames;}
    QAtomicInt* getAcqFrameNumPointer(){return m_acqFrameNum;}
    QAtomicInt* getDAQFrameNumPointer() { return m_daqFrameNum; }
    QString getDeviceName(){return m_deviceName;}
    bool getHeadOrienataionStreamState() { return m_headOrientationStreamState;}
    bool getHeadOrienataionFilterState() { return m_headOrientationFilterState;}

signals:
    // TODO: setup signals to configure camera in thread
    void setPropertyI2C(long preambleKey, QVector<quint8> packet);
    void onPropertyChanged(QString devieName, QString propName, QVariant propValue);
    void sendMessage(QString msg);
    void takeScreenShot(QString type);
    void setExtTriggerTrackingState(bool state);
    void extTriggered(bool state);
    void startRecording();
    void stopRecording();

public slots:
    void sendNewFrame();
    void testSlot(QString, double);
    void handlePropChangedSignal(QString type, double displayValue, double i2cValue, double i2cValue2);
    void handleTakeScreenShotSignal();
    void handleDFFSwitchChange(bool checked);
    void close();

private:
    void getMiniscopeConfig(QString deviceType);
    void configureMiniscopeControls();
    QVector<QMap<QString, int>> parseSendCommand(QJsonArray sendCommand);
    int processString2Int(QString s);
    QMap<QString,quint16> deviceAddr;

    int m_camConnected;
    NewQuickView *view;
    VideoStreamOCV *miniscopeStream;
    QThread *videoStreamThread;
    cv::Mat frameBuffer[FRAME_BUFFER_SIZE];
    cv::Mat tempFrame;
    qint64 timeStampBuffer[FRAME_BUFFER_SIZE];
//    float bnoBuffer[FRAME_BUFFER_SIZE*3];
    float bnoBuffer[FRAME_BUFFER_SIZE*5]; //w,x,y,z,norm
    QSemaphore *freeFrames;
    QSemaphore *usedFrames;
    QObject *rootObject;
    VideoDisplay *vidDisplay;
    QQuickItem *bnoDisplay;
    QTimer *timer;
//    QImage testImage;
    int m_previousDisplayFrameNum;
    QAtomicInt *m_acqFrameNum;
    QAtomicInt *m_daqFrameNum;

//    QAtomicInt *m_DAQTimeStamp;

    // User Config parameters
    QJsonObject m_ucMiniscope;
    int m_deviceID;
    QString m_deviceName;
    QString m_deviceType;
//    QMap<QString,double> m_ucParameters; // holds all number parameters


    QJsonObject m_cMiniscopes; // Consider renaming to not confuse with ucMiniscopes
    QMap<QString,QVector<QMap<QString, int>>> m_controlSendCommand;
    QMap<QString, int> m_sendCommand;

    bool m_headOrientationStreamState;
    bool m_headOrientationFilterState;
    QString m_compressionType;
    QString m_displatState;

    cv::Mat baselineFrameBuffer[BASELINE_FRAME_BUFFER_SIZE];
    cv::Mat baselineFrame;
    int baselineFrameBufWritePos;
    qint64 baselinePreviousTimeStamp;
};


#endif // MINISCOPE_H
