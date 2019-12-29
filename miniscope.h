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

#include "videostreamocv.h"
#include "videodisplay.h"
#include "newquickview.h"
#include <opencv2/opencv.hpp>


#define PROTOCOL_I2C            -2
#define PROTOCOL_SPI            -3
#define SEND_COMMAND_VALUE_H    -5
#define SEND_COMMAND_VALUE_L    -6
#define SEND_COMMAND_VALUE      -6
#define SEND_COMMAND_ERROR      -10

#define FRAME_BUFFER_SIZE   128


class Miniscope : public QObject
{
    Q_OBJECT
public:
    explicit Miniscope(QObject *parent = nullptr, QJsonObject ucMiniscope = QJsonObject());
    void createView();
    void connectSnS();
    void parseUserConfigMiniscope();
    void sendInitCommands();
    cv::Mat* getFrameBufferPointer(){return frameBuffer;}
    qint64* getTimeStampBufferPointer(){return timeStampBuffer;}
    int getBufferSize() {return FRAME_BUFFER_SIZE;}
    QSemaphore* getFreeFramesPointer(){return freeFrames;}
    QSemaphore* getUsedFramesPointer(){return usedFrames;}
    QString getDeviceName(){return m_deviceName;}
    // Todo: thread safe buffer



signals:
    // TODO: setup signals to configure camera in thread
    void setPropertyI2C(long preambleKey, QVector<quint8> packet);
    void onPropertyChanged(QString devieName, QString propName, double propValue);

public slots:
    void sendNewFrame();
    void testSlot(QString, double);
    void handlePropCangedSignal(QString type, double displayValue, double i2cValue);

private:
    void getMiniscopeConfig(QString deviceType);
    void configureMiniscopeControls();
    QVector<QMap<QString, int>> parseSendCommand(QJsonArray sendCommand);
    int processString2Int(QString s);

    NewQuickView *view;
    VideoStreamOCV *miniscopeStream;
    QThread *videoStreamThread;
    cv::Mat frameBuffer[FRAME_BUFFER_SIZE];
    qint64 timeStampBuffer[FRAME_BUFFER_SIZE];
    QSemaphore *freeFrames;
    QSemaphore *usedFrames;
    QObject *rootObject;
    VideoDisplay *vidDisplay;
    QTimer *timer;
//    QImage testImage;
    int m_previousDisplayFrameNum;
    QAtomicInt *m_acqFrameNum;

    // User Config parameters
    QJsonObject m_ucMiniscope;
    int m_deviceID;
    QString m_deviceName;
    QString m_deviceType;
    QMap<QString,double> m_ucParameters; // holds all number parameters


    QJsonObject m_cMiniscopes; // Consider renaming to not confuse with ucMiniscopes
    QMap<QString,QVector<QMap<QString, int>>> m_controlSendCommand;
    QMap<QString, int> m_sendCommand;

};


#endif // MINISCOPE_H
