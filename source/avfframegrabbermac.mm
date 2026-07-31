#include "avfframegrabbermac.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <condition_variable>
#include <mutex>

#include <QDebug>
#include <opencv2/imgproc.hpp>

// Compiled with ARC (see CMakeLists) - no manual retain/release.

// Delegate receiving frames on the capture dispatch queue. Keeps only the
// LATEST frame (a mailbox, not a queue): the consumer loop paces itself and
// the ring buffer downstream handles buffering; stale frames are worthless.
@interface MSFrameSink : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
{
@public
    std::mutex mutex;
    std::condition_variable frameArrived;
    cv::Mat latest;      // 8UC3 BGR mailbox slot (guarded by mutex)
    uint64_t sequence;   // bumps once per delivered frame
    OSType loggedFormat; // first delivered format, logged once
    cv::Mat scratch;     // delegate-only conversion target (serial queue, no lock)
}
@end

@implementation MSFrameSink
- (void)captureOutput:(AVCaptureOutput *)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection *)connection
{
    CVImageBufferRef image = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!image)
        return;
    CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    const int w = int(CVPixelBufferGetWidth(image));
    const int h = int(CVPixelBufferGetHeight(image));
    const size_t stride = CVPixelBufferGetBytesPerRow(image);
    const OSType format = CVPixelBufferGetPixelFormatType(image);
    void *base = CVPixelBufferGetBaseAddress(image);

    if (loggedFormat != format) {
        loggedFormat = format;
        const char fourcc[5] = {char(format >> 24), char(format >> 16),
                                char(format >> 8), char(format), 0};
        qInfo().nospace() << "[diag] grabber pixel format '" << fourcc << "' " << w << "x" << h
                          << " stride=" << stride;
    }

    if (base && w > 0 && h > 0) {
        // Interpret the buffer by what AVFoundation actually DELIVERED, never
        // by what was requested - a device may ignore the requested format
        // (reading YUY2 bytes as BGRA shows as garbled, row-shifted video).
        // Convert into the delegate-private scratch Mat WITHOUT the lock (this
        // runs on a serial dispatch queue), then swap it in: holding the lock
        // across a full-frame conversion stalls the capture queue against the
        // consumer's read() and turns contention into dropped frames.
        bool converted = true;
        switch (format) {
        case kCVPixelFormatType_32BGRA:
            cv::cvtColor(cv::Mat(h, w, CV_8UC4, base, stride), scratch, cv::COLOR_BGRA2BGR);
            break;
        case kCVPixelFormatType_422YpCbCr8:        // '2vuy' = UYVY
            cv::cvtColor(cv::Mat(h, w, CV_8UC2, base, stride), scratch, cv::COLOR_YUV2BGR_UYVY);
            break;
        case kCVPixelFormatType_422YpCbCr8_yuvs:   // 'yuvs' = YUYV / YUY2
        case kCVPixelFormatType_422YpCbCr8FullRange:
            cv::cvtColor(cv::Mat(h, w, CV_8UC2, base, stride), scratch, cv::COLOR_YUV2BGR_YUY2);
            break;
        case kCVPixelFormatType_24BGR:
            cv::Mat(h, w, CV_8UC3, base, stride).copyTo(scratch);
            break;
        default:
            // Unknown layout: drop the frame rather than mis-render it. The
            // one-time format log above tells us what to add.
            converted = false;
            break;
        }
        if (converted) {
            std::lock_guard<std::mutex> lock(mutex);
            cv::swap(scratch, latest);
            sequence++;
        }
    }
    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    frameArrived.notify_all();
}
@end

struct AvfFrameGrabber::Impl {
    AVCaptureSession *session = nil;
    MSFrameSink *sink = nil;
    dispatch_queue_t queue = nil;
    uint64_t consumedSequence = 0;
    // Device whose configuration lock we still hold, or nil. Holding the lock
    // for the session's whole lifetime is what stops AVCaptureSession from
    // reconfiguring activeFormat out from under us (see open()).
    AVCaptureDevice *lockedDevice = nil;
};

AvfFrameGrabber::~AvfFrameGrabber()
{
    release();
}

bool AvfFrameGrabber::open(const QString &uniqueID, int width, int height)
{
    release();
    @autoreleasepool {
        AVCaptureDevice *device =
            [AVCaptureDevice deviceWithUniqueID:uniqueID.toNSString()];
        if (!device) {
            m_lastError = QStringLiteral("no capture device with uniqueID %1").arg(uniqueID);
            return false;
        }

        NSError *error = nil;
        AVCaptureDeviceInput *input =
            [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
        if (!input) {
            m_lastError = QStringLiteral("could not open %1: %2")
                              .arg(QString::fromNSString(device.localizedName),
                                   QString::fromNSString(error.localizedDescription));
            return false;
        }

        AVCaptureSession *session = [[AVCaptureSession alloc] init];
        if (![session canAddInput:input]) {
            m_lastError = QStringLiteral("capture session rejected %1 (in use by another app?)")
                              .arg(QString::fromNSString(device.localizedName));
            return false;
        }
        [session addInput:input];

        // Put the DEVICE in the requested geometry, rather than taking whatever
        // format it happens to default to. A Miniscope DAQ advertises one format
        // per sensor mode of the family it supports (608x608 for a V4, 752x480
        // for a V3, 1024x768 for a MiniCAM, 1296x972 / 2592x1944 for the
        // MiniCAM's MT9P031 unbinned modes, ...) and its default activeFormat is
        // 608x608. Selecting nothing therefore silently pinned every device to
        // the Miniscope's geometry: a no-op for a Miniscope, and the reason a
        // MiniCAM delivered one 608x608 frame and then stalled.
        //
        // macOS has no AVCaptureSessionPresetInputPriority (iOS-only) to declare
        // that the device's own format wins, and the session WILL reconfigure
        // the device on startRunning() - measured: a MiniCAM pinned to 1024x768
        // came back as 1296x972. Holding the configuration lock across
        // startRunning() is what makes the choice stick, so the lock is released
        // in release(), not here. The read-back after startRunning() verifies it.
        const QString deviceLabel = QString::fromNSString(device.localizedName);
        AVCaptureDevice *lockedDevice = nil;
        bool formatSelected = false;
        if (width > 0 && height > 0) {
            AVCaptureDeviceFormat *match = nil;
            for (AVCaptureDeviceFormat *f in device.formats) {
                const CMVideoDimensions dim =
                    CMVideoFormatDescriptionGetDimensions(f.formatDescription);
                if (dim.width == width && dim.height == height) {
                    match = f;
                    break;
                }
            }
            if (match == nil) {
                qWarning() << "AVF:" << deviceLabel << "advertises no" << width << "x" << height
                           << "format; falling back to CoreVideo scaling of its default format";
            } else {
                NSError *lockError = nil;
                if (![device lockForConfiguration:&lockError]) {
                    qWarning() << "AVF: could not lock" << deviceLabel << "to select" << width
                               << "x" << height << "-"
                               << QString::fromNSString(lockError.localizedDescription);
                } else {
                    device.activeFormat = match;
                    lockedDevice = device;   // stays locked until release()
                    formatSelected = true;
                    qInfo().nospace() << "[diag] " << deviceLabel << " activeFormat set to "
                                      << width << "x" << height;
                }
            }
        }

        AVCaptureVideoDataOutput *output = [[AVCaptureVideoDataOutput alloc] init];
        NSMutableDictionary *settings = [@{
            (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
        } mutableCopy];
        if (width > 0 && height > 0 && !formatSelected) {
            // Only when no matching device format exists. Resampling imaging
            // data is lossy, so it stays a fallback rather than the mechanism.
            settings[(id)kCVPixelBufferWidthKey] = @(width);
            settings[(id)kCVPixelBufferHeightKey] = @(height);
        }
        output.videoSettings = settings;
        output.alwaysDiscardsLateVideoFrames = YES;

        MSFrameSink *sink = [MSFrameSink new];
        dispatch_queue_t queue =
            dispatch_queue_create("org.aharonilab.miniscope.avfgrabber", DISPATCH_QUEUE_SERIAL);
        [output setSampleBufferDelegate:sink queue:queue];
        if (![session canAddOutput:output]) {
            m_lastError = QStringLiteral("capture session rejected the video output");
            [lockedDevice unlockForConfiguration];   // no-op when nil
            return false;
        }
        [session addOutput:output];
        [session startRunning];

        // Did the format survive session start? A silent revert here is the
        // difference between imaging the sensor's real geometry and imaging a
        // rescaled crop of someone else's, so it must be loud.
        if (formatSelected) {
            const CMVideoDimensions actual =
                CMVideoFormatDescriptionGetDimensions(device.activeFormat.formatDescription);
            if (actual.width != width || actual.height != height)
                qWarning() << "AVF:" << deviceLabel << "reverted to" << actual.width << "x"
                           << actual.height << "when the session started (asked for" << width
                           << "x" << height << ")";
            else
                qInfo().nospace() << "[diag] " << deviceLabel << " activeFormat held at "
                                  << width << "x" << height << " after session start";
        }

        m_impl = new Impl;
        m_impl->session = session;
        m_impl->sink = sink;
        m_impl->queue = queue;
        m_impl->lockedDevice = lockedDevice;
    }
    m_lastError.clear();
    return true;
}

bool AvfFrameGrabber::isOpened() const
{
    return m_impl && m_impl->session.running;
}

void AvfFrameGrabber::release()
{
    if (!m_impl)
        return;
    @autoreleasepool {
        [m_impl->session stopRunning];
        for (AVCaptureOutput *out in m_impl->session.outputs)
            if ([out isKindOfClass:[AVCaptureVideoDataOutput class]])
                [(AVCaptureVideoDataOutput *)out setSampleBufferDelegate:nil queue:nil];
        // Held since open() to pin activeFormat; drop it only now that the
        // session is stopped, so the device is left configurable for the next
        // open (ours or another app's).
        [m_impl->lockedDevice unlockForConfiguration];   // no-op when nil
        m_impl->lockedDevice = nil;
    }
    delete m_impl;   // ARC releases the session/sink/queue with the Impl
    m_impl = nullptr;
}

bool AvfFrameGrabber::read(cv::Mat &frame, int timeoutMs)
{
    if (!m_impl) {
        m_lastError = QStringLiteral("grabber is not open");
        return false;
    }
    MSFrameSink *sink = m_impl->sink;
    std::unique_lock<std::mutex> lock(sink->mutex);
    const bool arrived = sink->frameArrived.wait_for(
        lock, std::chrono::milliseconds(timeoutMs),
        [&] { return sink->sequence != m_impl->consumedSequence; });
    if (!arrived) {
        m_lastError = QStringLiteral("no frame within %1 ms").arg(timeoutMs);
        return false;
    }
    // Swap, don't copy: the consumer's previous frame buffer becomes the next
    // mailbox slot, so steady state runs allocation- and copy-free here.
    cv::swap(sink->latest, frame);
    m_impl->consumedSequence = sink->sequence;
    return true;
}
