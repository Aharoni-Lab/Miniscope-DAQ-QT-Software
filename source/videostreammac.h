#ifndef VIDEOSTREAMMAC_H
#define VIDEOSTREAMMAC_H

// Hybrid macOS capture backend for Miniscopes - see videostreambase.h.
//
// Frames stream through OpenCV's AVFoundation backend like any webcam (the
// Miniscope enumerates as a standard UVC camera), while device control - the
// I2C-over-UVC command tunnel, the per-frame DAQ frame counter, and the BNO
// head-orientation registers - goes through UVCControlMac's IOKit pipe-0
// requests, which coexist with Apple's streaming driver. Neither half works
// alone on macOS: AVFoundation exposes no UVC controls, and owning the whole
// device (the Linux libuvc approach) needs root here.
//
// Latency note (measured against real hardware): a pipe-0 GET_CUR costs
// ~1 ms idle but ~6 ms while the device streams. Reads are therefore gated
// exactly like the other backends - frame counter always (1 read/frame), BNO
// only when head orientation is enabled, trigger state only when tracking.

#include <QSemaphore>
#include <QAtomicInt>
#include <QVector>
#include <opencv2/core/core.hpp>

#include "avfenumeratormac.h"
#include "avfframegrabbermac.h"
#include "miniscopeprotocol.h"
#include "uvccontrolmac.h"
#include "videostreambase.h"

class VideoStreamMac : public VideoStreamBase
{
    Q_OBJECT
public:
    explicit VideoStreamMac(QObject *parent = nullptr, int width = 0, int height = 0, double pixelClock = 0);
    ~VideoStreamMac() override;

    void setBufferParameters(cv::Mat *frameBuf, qint64 *tsBuf, float *bnoBuf,
                             int bufferSize, QSemaphore *freeFramesS, QSemaphore *usedFramesS,
                             QAtomicInt *acqFrameNum, QAtomicInt *daqFrameNumber) override;
    int connect2Camera(int cameraID) override;
    int connect2Video(QString folderPath, QString filePrefix, float playbackFPS) override;
    void setHeadOrientationConfig(bool enableState, bool) override { m_headOrientationStreamState = enableState; }
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
    // AVFoundation index -> USB locationID -> UVCControlMac (policy in resolveControlTarget)
    bool openControlForIndex(int cameraID, const QVector<AvfCameraInfo> &cameras);
    // Pin the grabber to the control channel's device via its uniqueID.
    bool openFrameStream(const QVector<AvfCameraInfo> &cameras);
    void sendCommands() override;             // flush queued I2C packets as UVC SET_CUR
    bool setPU(quint8 selector, quint16 value);
    int  getPU(quint8 selector);              // fresh GET_CUR, returned as signed 16-bit; 0 on failure
    bool attemptReconnect();
    void logStallDiagnosis();                 // frame-counter poll: AVF session vs scope video link

    int m_cameraID;
    QString m_deviceName;

    AvfFrameGrabber m_grabber;
    UVCControlMac m_control;

    bool m_stopStreaming;
    bool m_headOrientationStreamState;
    bool m_isColor;

    cv::Mat *frameBuffer;
    qint64 *timeStampBuffer;
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
};

#endif // VIDEOSTREAMMAC_H
