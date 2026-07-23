#ifndef VIDEOSTREAMLIBUVC_H
#define VIDEOSTREAMLIBUVC_H

// libuvc capture backend - Linux only. Built only when CMake finds libuvc and
// defines HAVE_LIBUVC. See videostreambase.h for why this exists (uvcvideo
// caches UVC control reads; libuvc GET_CUR bypasses that cache so the live
// frame counter and BNO head-orientation registers can be read on Linux).
#ifdef HAVE_LIBUVC

#include <atomic>
#include <QSemaphore>
#include <QAtomicInt>
#include <QVector>
#include <opencv2/core/core.hpp>

#include <libuvc/libuvc.h>

#include "miniscopeprotocol.h"
#include "videostreambase.h"

class VideoStreamLibUVC : public VideoStreamBase
{
    Q_OBJECT
public:
    explicit VideoStreamLibUVC(QObject *parent = nullptr, int width = 0, int height = 0, double pixelClock = 0);
    ~VideoStreamLibUVC() override;

    void setBufferParameters(cv::Mat *frameBuf, qint64 *tsBuf, float *bnoBuf,
                             qint64 *daqFrameNumBuf,
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
    // UVC selectors / unit ID / VID+PID come from miniscopeprotocol.h (shared
    // with the macOS IOKit control transport).
    bool openByVideoIndex(int cameraID);   // resolve /dev/videoN -> USB bus/addr, open via libuvc
    bool negotiateFormat();
    void sendCommands() override;          // flush queued I2C packets as UVC SET_CUR
    bool setPU(quint8 selector, quint16 value);
    int  getPU(quint8 selector);           // fresh GET_CUR, returned as signed 16-bit
    bool attemptReconnect();
    void closeStream();
    void closeDevice();

    int m_cameraID;
    QString m_deviceName;

    uvc_context_t *m_ctx;
    uvc_device_t *m_dev;
    uvc_device_handle_t *m_devh;
    uvc_stream_ctrl_t m_streamCtrl;
    uvc_stream_handle_t *m_strmh;
    int m_negotiatedFps;

    std::atomic<bool> m_isStreaming;
    std::atomic<bool> m_stopStreaming;
    bool m_headOrientationStreamState;
    bool m_headOrientationFilterState;
    bool m_isColor;

    cv::Mat *frameBuffer;
    qint64 *timeStampBuffer;
    qint64 *daqFrameNumBuffer;
    float *bnoBuffer;
    QSemaphore *freeFrames;
    QSemaphore *usedFrames;
    int frameBufferSize;
    QAtomicInt *m_acqFrameNum;
    QAtomicInt *daqFrameNum;

    I2CCommandQueue m_commandQueue;

    bool m_trackExtTrigger;

    int m_expectedWidth;
    int m_expectedHeight;
    double m_pixelClock;
    QString m_connectionType;
};

#endif // HAVE_LIBUVC
#endif // VIDEOSTREAMLIBUVC_H
