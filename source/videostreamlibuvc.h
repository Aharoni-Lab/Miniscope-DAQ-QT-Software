#ifndef VIDEOSTREAMLIBUVC_H
#define VIDEOSTREAMLIBUVC_H

// libuvc capture backend - Linux only. Built only when CMake finds libuvc and
// defines HAVE_LIBUVC. See videostreambase.h for why this exists (uvcvideo
// caches UVC control reads; libuvc GET_CUR bypasses that cache so the live
// frame counter and BNO head-orientation registers can be read on Linux).
#ifdef HAVE_LIBUVC

#include <QSemaphore>
#include <QAtomicInt>
#include <QMap>
#include <QVector>
#include <opencv2/core/core.hpp>

#include <libuvc/libuvc.h>

#include "videostreambase.h"

class VideoStreamLibUVC : public VideoStreamBase
{
    Q_OBJECT
public:
    explicit VideoStreamLibUVC(QObject *parent = nullptr, int width = 0, int height = 0, double pixelClock = 0);
    ~VideoStreamLibUVC() override;

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
    // UVC Processing-Unit control selectors the Miniscope overloads.
    enum PuSelector {
        SEL_BRIGHTNESS = 0x02, // BNO quaternion z
        SEL_CONTRAST   = 0x03, // I2C low word (write) / DAQ frame number (read)
        SEL_GAIN       = 0x04, // BNO quaternion y
        SEL_HUE        = 0x06, // BNO quaternion x
        SEL_SATURATION = 0x07, // data-stream "start" (write) / BNO quaternion w (read)
        SEL_SHARPNESS  = 0x08, // I2C high word
        SEL_GAMMA      = 0x09  // I2C mid word (write) / external trigger state (read)
    };

    bool openByVideoIndex(int cameraID);   // resolve /dev/videoN -> USB bus/addr, open via libuvc
    bool negotiateFormat();
    void sendSerdesModeCommands();         // pixel-clock dependent SERDES setup
    void sendCommands();                   // flush queued I2C packets as UVC SET_CUR
    bool setPU(quint8 selector, quint16 value);
    int  getPU(quint8 selector);           // fresh GET_CUR, returned as signed 16-bit
    bool attemptReconnect();
    void closeStream();
    void closeDevice();

    static const uint8_t PROCESSING_UNIT_ID = 2; // from the Miniscope UVC descriptor

    int m_cameraID;
    QString m_deviceName;

    uvc_context_t *m_ctx;
    uvc_device_t *m_dev;
    uvc_device_handle_t *m_devh;
    uvc_stream_ctrl_t m_streamCtrl;
    uvc_stream_handle_t *m_strmh;
    int m_negotiatedFps;

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

    QVector<long> sendCommandQueueOrder;
    QMap<long, QVector<quint8>> sendCommandQueue;

    bool m_trackExtTrigger;

    int m_expectedWidth;
    int m_expectedHeight;
    double m_pixelClock;
    QString m_connectionType;
};

#endif // HAVE_LIBUVC
#endif // VIDEOSTREAMLIBUVC_H
