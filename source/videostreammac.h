#ifndef VIDEOSTREAMMAC_H
#define VIDEOSTREAMMAC_H

// Hybrid macOS capture backend for Miniscopes - see videostreambase.h.
//
// Frames stream through an AVFoundation capture session pinned to the device
// uniqueID (the Miniscope enumerates as a standard UVC camera), while device
// control - the I2C-over-UVC command tunnel, the per-frame DAQ frame counter,
// and the BNO head-orientation registers - goes through UVCControlMac's IOKit
// pipe-0 requests, which coexist with Apple's streaming driver. Neither half
// works alone on macOS: AVFoundation exposes no UVC controls, and owning the
// whole device (the Linux libuvc approach) needs root here.
//
// Latency note (measured against real hardware): a pipe-0 GET_CUR costs
// ~1 ms idle but ~6 ms while the device streams. Reads are therefore gated
// exactly like the other backends - frame counter always (1 read/frame), BNO
// only when head orientation is enabled, trigger state only when tracking.

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

    int connect2Camera(int cameraID) override;

public slots:
    void startStream() override;
    void startRecording() override;
    void stopRecording() override;

protected:
    bool writeControlWord(quint8 selector, quint16 word) override;   // IOKit SET_CUR
    bool readControl(quint8 selector, quint16 *value) override;      // IOKit GET_CUR
    bool attemptReconnect() override;
    void diagnoseStreamFailure() override;   // frame-counter poll: AVF session vs scope video link

private:
    // AVFoundation index -> USB locationID -> UVCControlMac (policy in resolveControlTarget)
    bool openControlForIndex(int cameraID, const QVector<AvfCameraInfo> &cameras);
    // Pin the grabber to the control channel's device via its uniqueID.
    bool openFrameStream(const QVector<AvfCameraInfo> &cameras);
    void logStallDiagnosis();

    AvfFrameGrabber m_grabber;
    UVCControlMac m_control;
};

#endif // VIDEOSTREAMMAC_H
