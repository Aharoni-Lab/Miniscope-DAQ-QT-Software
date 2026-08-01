#ifndef AVFFRAMEGRABBERMAC_H
#define AVFFRAMEGRABBERMAC_H

#include <QString>
#include <opencv2/core/core.hpp>

// Native AVFoundation frame grabber pinned to a device uniqueID.
//
// Why not cv::VideoCapture: OpenCV's AVFoundation backend can only open a
// camera by LIST INDEX, and macOS reshuffles the camera list whenever devices
// come and go (iPhone Continuity Camera, hot-plugged webcams). Bench-verified
// failure mode: the index resolved to the Miniscope at lookup time but to a
// different camera by open time, so the app streamed the wrong device while
// the control channel drove the real scope. uniqueID is stable for the
// lifetime of the connection, so the session opened here cannot land on the
// wrong camera by construction.
//
// Frames are delivered as 8UC3 BGR cv::Mat (matching what the OpenCV path
// produced), at the device's own format where the requested size is one of the
// modes it advertises, and rescaled by CoreVideo only where it is not.
class AvfFrameGrabber
{
public:
    AvfFrameGrabber() = default;
    ~AvfFrameGrabber();
    AvfFrameGrabber(const AvfFrameGrabber &) = delete;
    AvfFrameGrabber &operator=(const AvfFrameGrabber &) = delete;

    // uniqueID: AVCaptureDevice.uniqueID (see AvfCameraInfo). width/height > 0
    // puts the DEVICE in that mode when it advertises a matching format, and
    // falls back to CoreVideo-scaled output buffers when it does not (logged as
    // a warning - it means the frames are resampled). 0 keeps whatever format
    // the device defaults to.
    bool open(const QString &uniqueID, int width = 0, int height = 0);
    bool isOpened() const;
    void release();

    // Blocks until the NEXT frame arrives (or timeout). Returns false on
    // timeout or when not open - the caller treats that like a failed grab.
    bool read(cv::Mat &frame, int timeoutMs = 5000);

    QString lastError() const { return m_lastError; }

private:
    struct Impl;
    Impl *m_impl = nullptr;
    QString m_lastError;
};

#endif // AVFFRAMEGRABBERMAC_H
