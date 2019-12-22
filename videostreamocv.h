#ifndef VIDEOSTREAMOCV_H
#define VIDEOSTREAMOCV_H

#include <QObject>
#include <QSemaphore>
#include <QString>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <QAtomicInt>
#include <QMap>
#include <QVector>


class VideoStreamOCV : public QObject
{
    Q_OBJECT
public:
    explicit VideoStreamOCV(QObject *parent = nullptr);
    ~VideoStreamOCV();
    void setCameraID(int cameraID);
    void setBufferParameters(cv::Mat *buf, int bufferSize, QSemaphore *freeFramesS, QSemaphore *usedFramesS, QAtomicInt *acqFrameNum);


signals:

public slots:
    void startStream();
    void stopSteam();
    void setPropertyI2C(long preambleKey, QVector<quint8> packet);

private:
    void sendCommands();
    int m_cameraID;
    cv::VideoCapture *cam;
    bool m_isStreaming;
    bool m_stopStreaming;
    cv::Mat *buffer;
    QSemaphore *freeFrames;
    QSemaphore *usedFrames;
    int frameBufferSize;
    QAtomicInt *m_acqFrameNum;
    QMap<long, QVector<quint8>> sendCommandQueue;


};

#endif // VIDEOSTREAMOCV_H
