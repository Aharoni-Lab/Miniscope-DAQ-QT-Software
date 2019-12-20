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

#include "videostreamocv.h"
#include "videodisplay.h"
#include <opencv2/opencv.hpp>

#define PROTOCOL_I2C        -2
#define PROTOCOL_SPI        -3
#define SEND_COMMAND_VALUE  -5
#define SEND_COMMAND_ERROR  -10

#define FRAME_BUFFER_SIZE   128

class NewQuickView: public QQuickView {
    // Class makes it so we can handle window closing events
    // TODO: if recording don't release camera
    Q_OBJECT
public:
    NewQuickView(QUrl url):
        QQuickView(url) {}
public:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::Close) {
            // your code here
            qDebug() << "CLOSEING!!";
            emit closing();
        }
        return QQuickView::event(event);
    }
signals:
    void closing();

public slots:

};


class Miniscope : public QObject
{
    Q_OBJECT
public:
    explicit Miniscope(QObject *parent = nullptr, QJsonObject ucMiniscope = QJsonObject());
    void createView();
    void connectSnS();
    void parseUserConfigMiniscope();
    // Todo: thread safe buffer



signals:
    // TODO: setup signals to configure camera in thread
    void setPropertyI2C(unsigned int preample, unsigned int data);

public slots:
    void sendNewFrame();
    void testSlot(QString, double);
    void handlePropCangedSignal(QString type, double value);

private:
    void getMiniscopeConfig();
    void configureMiniscopeControls();
    QMap<QString, unsigned int> parseSendCommand(QJsonObject sendCommand);
    int processString2Int(QString s);

    NewQuickView *view;
    VideoStreamOCV *miniscopeStream;
    QThread *videoStreamThread;
    cv::Mat frameBuffer[FRAME_BUFFER_SIZE];
    QSemaphore *freeFrames;
    QSemaphore *usedFrames;
    QObject *rootObject;
    VideoDisplay *vidDisplay;
    QTimer *timer;
//    QImage testImage;
    int m_previousDisplayFrameNum;
    QAtomicInt *m_acqFrameNum;
    QJsonObject m_ucMiniscope;
    int m_deviceID;
    QString m_deviceName;
    QString m_deviceType;
    QJsonObject m_cMiniscopes; // Consider renaming to not confuse with ucMiniscopes
    QMap<QString,QMap<QString, unsigned int>> m_controlSendCommand;
    QMap<QString, unsigned int> m_sendCommand;

};


#endif // MINISCOPE_H
