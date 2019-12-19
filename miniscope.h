#ifndef MINISCOPE_H
#define MINISCOPE_H

#include <QObject>
#include <QThread>
#include <QSemaphore>
#include <QTimer>
#include <QAtomicInt>
#include <QJsonObject>
#include <QQuickView>

#include "videostreamocv.h"
#include "videodisplay.h"
#include <opencv2/opencv.hpp>

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

public slots:
    void sendNewFrame();
    void testSlot(QString, double);

private:
    void getMiniscopeConfig();
    void configureMiniscopeControls();

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


};


#endif // MINISCOPE_H
