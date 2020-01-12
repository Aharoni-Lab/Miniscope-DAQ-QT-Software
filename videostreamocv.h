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
//    void setCameraID(int cameraID);
    void setBufferParameters(cv::Mat *frameBuf, qint64 *tsBuf, float *bnoBuf,
                             int bufferSize, QSemaphore *freeFramesS, QSemaphore *usedFramesS,
                             QAtomicInt *acqFrameNum, QAtomicInt *daqFrameNumber);
    int connect2Camera(int cameraID);
    void setStreamHeadOrientation(bool streamState) { m_streamHeadOrientationState = streamState; }
    void setIsColor(bool isColor) { m_isColor = isColor; }
    void setDeviceName(QString name) { m_deviceName = name; }

signals:
    void sendMessage(QString msg);

public slots:
    void startStream();
    void stopSteam();
    void setPropertyI2C(long preambleKey, QVector<quint8> packet);

private:
    void sendCommands();
    int m_cameraID;
    QString m_deviceName;
    cv::VideoCapture *cam;
    bool m_isStreaming;
    bool m_stopStreaming;
    bool m_streamHeadOrientationState;
    bool m_isColor;
    cv::Mat *frameBuffer;
    qint64 *timeStampBuffer;
    float *bnoBuffer;
    QSemaphore *freeFrames;
    QSemaphore *usedFrames;
    int frameBufferSize;
    QAtomicInt *m_acqFrameNum;
    QAtomicInt *daqFrameNum;

    // Handles commands sent to video stream device
    QVector<long> sendCommandQueueOrder;
    QMap<long, QVector<quint8>> sendCommandQueue;


};

#endif // VIDEOSTREAMOCV_H
