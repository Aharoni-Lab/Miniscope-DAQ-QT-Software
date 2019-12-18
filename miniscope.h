#ifndef MINISCOPE_H
#define MINISCOPE_H

#include <QObject>
#include <QThread>
#include <QSemaphore>
#include <QTimer>
#include <QAtomicInt>

#include "videostreamocv.h"
#include "videodisplay.h"
#include <opencv2/opencv.hpp>

#define FRAME_BUFFER_SIZE   128

class Miniscope : public QObject
{
    Q_OBJECT
public:
    explicit Miniscope(QObject *parent = nullptr);
    void createView();
    void connectSnS();
    // Todo: thread safe buffer



signals:
    // TODO: setup signals to configure camera in thread

public slots:
    void sendNewFrame();
    void testSlot(QString, double);

private:
    VideoStreamOCV *miniscopeStream;
    QThread *videoStreamThread;
    cv::Mat frameBuffer[FRAME_BUFFER_SIZE];
    QSemaphore *freeFrames;
    QSemaphore *usedFrames;
    QObject *rootObject;
    VideoDisplay *vidDisplay;
    QTimer *timer;
    QImage testImage;
    int m_previousDisplayFrameNum;
    QAtomicInt *m_acqFrameNum;


};

#endif // MINISCOPE_H
