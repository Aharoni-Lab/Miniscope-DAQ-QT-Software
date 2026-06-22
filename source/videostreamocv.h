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

#include "videostreambase.h"


class VideoStreamOCV : public VideoStreamBase
{
    Q_OBJECT
public:
    explicit VideoStreamOCV(QObject *parent = nullptr, int width = 0, int height = 0, double pixelClock = 0);
    ~VideoStreamOCV() override;
//    void setCameraID(int cameraID);
    void setBufferParameters(cv::Mat *frameBuf, qint64 *tsBuf, float *bnoBuf,
                             int bufferSize, QSemaphore *freeFramesS, QSemaphore *usedFramesS,
                             QAtomicInt *acqFrameNum, QAtomicInt *daqFrameNumber) override;
    int connect2Camera(int cameraID) override;
    int connect2Video(QString folderPath, QString filePrefix, float playbackFPS) override;
    void setHeadOrientationConfig(bool enableState, bool filterState) override { m_headOrientationStreamState = enableState; m_headOrientationFilterState = filterState; }
    void setIsColor(bool isColor) override { m_isColor = isColor; }
    void setDeviceName(QString name) override { m_deviceName = name; }

public slots:
    void startStream() override;
    void stopSteam() override;
    void setPropertyI2C(long preambleKey, QVector<quint8> packet) override;
    void setExtTriggerTrackingState(bool state) override;
    void startRecording() override;
    void stopRecording() override;
    void openCamPropsDialog() override;

private:
    void sendCommands();
    bool attemptReconnect();
    int m_cameraID;
    QString m_deviceName;
    cv::VideoCapture *cam;
    bool m_isStreaming;
    bool m_stopStreaming;
    bool m_headOrientationStreamState;
    bool m_headOrientationFilterState;
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

    bool m_trackExtTrigger;

    int m_expectedWidth;
    int m_expectedHeight;
    double m_pixelClock;

    QString m_connectionType;

    double m_playbackFPS;
    QString m_playbackFolderPath;
    QString m_playbackFilePrefix;
    int m_playbackFileIndex;

};

#endif // VIDEOSTREAMOCV_H
