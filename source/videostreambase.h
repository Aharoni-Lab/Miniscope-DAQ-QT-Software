#ifndef VIDEOSTREAMBASE_H
#define VIDEOSTREAMBASE_H

#include <atomic>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QVector>
#include <opencv2/core/core.hpp>

#include "miniscopeprotocol.h"

// Bench diagnostics (per-frame heartbeats, stall verdicts, pin lines) shared
// by every backend. On by default; silence with
// QT_LOGGING_RULES="miniscope.diag=false".
Q_DECLARE_LOGGING_CATEGORY(msDiag)

class QSemaphore;
class QAtomicInt;

// Reconnect pacing shared by every backend's stream loop: message/diagnose on
// the FIRST failure of an episode only, then back off 1 s, 2 s, ... capped at
// 5 s (a device that fell off the bus can take a while to come back, and every
// retry already logs). Pure state - pinned down in tst_videostreambase.
class ReconnectBackoff
{
public:
    bool firstFailure() const { return m_attempts == 0; }
    int attempts() const { return m_attempts; }
    // Registers one failed attempt and returns how long to sleep before retrying.
    int nextDelayMs()
    {
        m_attempts++;
        return m_attempts < 5 ? 1000 * m_attempts : 5000;
    }
    void reset() { m_attempts = 0; }

private:
    int m_attempts = 0;
};

// Abstract capture-backend interface for a single video device.
//
// Three concrete backends implement this:
//   * VideoStreamOCV     - OpenCV VideoCapture (V4L2 on Linux, DirectShow on
//                          Windows). Used for all devices on Windows, for
//                          behaviour cameras everywhere, and for video-file
//                          playback. On macOS live cameras stream through an
//                          AVFoundation grabber pinned to the device uniqueID.
//   * VideoStreamLibUVC  - libuvc/libusb direct UVC access (Linux only). Used
//                          for Miniscopes on Linux, where the kernel uvcvideo
//                          driver caches UVC control reads and so cannot return
//                          the live frame counter / BNO head-orientation
//                          registers that the Miniscope streams back through
//                          GET_CUR. A fresh libuvc GET_CUR bypasses that cache.
//   * VideoStreamMac     - AVFoundation frames + IOKit pipe-0 control
//                          requests (macOS only). Used for Miniscopes on
//                          macOS, where AVFoundation exposes no UVC controls
//                          and owning the whole device needs root.
//
// VideoDevice owns one of these, moves it to its own QThread, and talks to it
// purely through this interface (direct calls + queued signals/slots), so the
// backend is interchangeable.
//
// The backends differ only in transport: how a frame is acquired, how a UVC
// Processing-Unit control is read/written, and how a dead device is reopened.
// Everything that happens between acquiring a frame and handing it to the
// rest of the app - ring-buffer slot bookkeeping, timestamps, external
// trigger edges, BNO quaternion capture, the DAQ hardware frame counter -
// lives here in commitFrame(), so it cannot drift between platforms again.
class VideoStreamBase : public QObject
{
    Q_OBJECT
public:
    explicit VideoStreamBase(QObject *parent = nullptr,
                             int width = 0, int height = 0, double pixelClock = 0);
    ~VideoStreamBase() override = default;

    // daqFrameNumBuf: per-slot copy of the DAQ hardware frame counter at the
    // moment each frame was acquired, so DataSaver can log it per frame in
    // timeStamps.csv (makes USB frame loss detectable post-hoc). Slots whose
    // counter could not be read hold -1.
    void setBufferParameters(cv::Mat *frameBuf, qint64 *tsBuf, float *bnoBuf,
                             qint64 *daqFrameNumBuf,
                             int bufferSize, QSemaphore *freeFramesS, QSemaphore *usedFramesS,
                             QAtomicInt *acqFrameNum, QAtomicInt *daqFrameNumber);
    virtual int connect2Camera(int cameraID) = 0;
    // Default: not supported (video-file playback always uses the OpenCV
    // backend, which overrides this).
    virtual int connect2Video(QString folderPath, QString filePrefix, float playbackFPS);
    // filterState is consumed by DataSaver, not the capture backends.
    void setHeadOrientationConfig(bool enableState, bool filterState)
    {
        m_headOrientationStreamState = enableState;
        Q_UNUSED(filterState);
    }
    void setIsColor(bool isColor) { m_isColor = isColor; }
    void setDeviceName(QString name) { m_deviceName = name; }

    // Estimated frames dropped between the DAQ hardware and the software this
    // run, for the live GUI readout. Rebased to the current connection epoch: a
    // reconnect restarts the DAQ frame counter (so the recorded CSV shows the
    // reset visibly), which would otherwise make this difference go permanently
    // negative and pin the readout at "N/A". Returns -1 ("N/A") when there is no
    // DAQ frame counter (behavior cams) or before the first successful read.
    int droppedFrameEstimate() const
    {
        if (daqFrameNum == nullptr || m_acqFrameNum == nullptr || !m_daqOffsetSeeded)
            return -1;
        return daqFrameNum->loadRelaxed() - (m_acqFrameNum->loadRelaxed() - m_acqAtDaqSeed);
    }

signals:
    void sendMessage(QString msg);
    void newFrameAvailable(QString name, int frameNum);
    void extTriggered(bool triggerState);
    void requestInitCommands();

public slots:
    virtual void startStream() = 0;
    void stopStream() { m_stopStreaming = true; }
    // A newer packet for the same preamble key replaces the unsent older one.
    void setPropertyI2C(long preambleKey, QVector<quint8> packet)
    {
        m_commandQueue.set(preambleKey, packet);
    }
    void setExtTriggerTrackingState(bool state) { m_trackExtTrigger = state; }
    virtual void startRecording() = 0;
    virtual void stopRecording() = 0;
    // OpenCV/DirectShow-only feature (behaviour cameras); no-op elsewhere.
    virtual void openCamPropsDialog() {}

protected:
    // ---- per-backend transport hooks -------------------------------------
    // Write one 16-bit word to a UVC Processing-Unit control (the I2C tunnel).
    virtual bool writeControlWord(quint8 selector, quint16 word) = 0;
    // Fresh GET_CUR of a Processing-Unit control. MUST return false when the
    // value could not be read - a fabricated 0 is indistinguishable from data
    // (it fakes trigger edges and corrupts the DAQ counter offset).
    virtual bool readControl(quint8 selector, quint16 *value) = 0;
    // Reopen a dead device, restoring SERDES mode and init commands. Called
    // from runReconnectCycle after the backoff sleep.
    virtual bool attemptReconnect() = 0;
    // Write the acquired frame into a ring-buffer slot. Default handles the
    // BGR backends (copy when color, BGR2GRAY otherwise); libuvc overrides
    // for its raw-YUYV frames.
    virtual void convertToSlot(const cv::Mat &frame, cv::Mat &slot);
    // Called once per committed frame, after the slot is published. The
    // default logs a heartbeat to msDiag every 100 frames, reusing the DAQ
    // counter read commitFrame already did (never issue a control read from
    // here - they cost ~6 ms on some transports).
    virtual void onFrameCommitted(int streamIdx, const cv::Mat &frame, qint64 timestampMs);
    // Called on the first failure of a reconnect episode, right after the
    // user-facing warning. Backends may run stall diagnostics here.
    virtual void diagnoseStreamFailure() {}

    // ---- shared machinery -------------------------------------------------
    // Flush queued I2C command packets through writeControlWord.
    virtual void sendCommands();

    // Queue + flush the pixel-clock dependent SERDES setup packets, then let
    // the video link settle. One shared copy of this order-critical connect
    // ritual - the pre-refactor code had four inline copies that had drifted.
    void sendSerdesModeCommands();

    // Reset all per-run stream-loop state. Call at the top of startStream().
    void resetStreamState();

    // The shared per-frame block: enforce the frame size, reserve a ring-buffer
    // slot, stamp/convert/read into it, publish it. Safe to call with any
    // frame the backend acquired; drops (with a message) when the buffer is
    // full or the size stops matching the locked stream size.
    void commitFrame(const cv::Mat &frame, qint64 timestampMs);

    // One reconnect attempt with shared pacing: first-failure message +
    // diagnoseStreamFailure(), backoff sleep, keep the event loop alive so
    // stopStream() can land, then attemptReconnect(). Returns true when the
    // device is back. `what` names the failed operation ("grab frame").
    bool runReconnectCycle(ReconnectBackoff &backoff, const QString &what);

    // Deliver queued cross-thread calls (setPropertyI2C etc.) and flush the
    // command queue. Call once per loop iteration.
    void serviceCommandQueue();

    // ---- shared state -----------------------------------------------------
    QString m_deviceName;
    int m_cameraID = -1;

    std::atomic<bool> m_stopStreaming{false};
    bool m_headOrientationStreamState = false;
    bool m_isColor = false;
    bool m_trackExtTrigger = false;

    int m_expectedWidth;
    int m_expectedHeight;
    double m_pixelClock;

    cv::Mat *frameBuffer = nullptr;
    qint64 *timeStampBuffer = nullptr;
    qint64 *daqFrameNumBuffer = nullptr;
    float *bnoBuffer = nullptr;
    QSemaphore *freeFrames = nullptr;
    QSemaphore *usedFrames = nullptr;
    int frameBufferSize = 0;
    QAtomicInt *m_acqFrameNum = nullptr;
    QAtomicInt *daqFrameNum = nullptr;

    I2CCommandQueue m_commandQueue;

    // Per-run stream-loop state (owned by commitFrame; reset by resetStreamState).
    int m_streamIdx = 0;               // frames committed this run (ring-buffer cursor)
    int m_daqFrameNumOffset = 0;
    bool m_daqOffsetSeeded = false;
    int m_acqAtDaqSeed = 0;            // acq count when the DAQ offset was (re)seeded; epoch base for droppedFrameEstimate()
    int m_extTriggerLast = -1;         // -1 = not primed yet
    cv::Size m_lockedFrameSize;        // locked to the first committed frame
    bool m_sizeMismatchWarned = false;
    float m_lastBnoQuat[5];            // last good {w,x,y,z,normError}
};

#endif // VIDEOSTREAMBASE_H
