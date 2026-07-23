#include "avfframegrabbermac.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

#include <condition_variable>
#include <mutex>

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
    cv::Mat latest;      // 8UC3 BGR
    uint64_t sequence;   // bumps once per delivered frame
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
    void *base = CVPixelBufferGetBaseAddress(image);
    if (base && w > 0 && h > 0) {
        const cv::Mat bgra(h, w, CV_8UC4, base, stride);
        std::lock_guard<std::mutex> lock(mutex);
        cv::cvtColor(bgra, latest, cv::COLOR_BGRA2BGR);
        sequence++;
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

        AVCaptureVideoDataOutput *output = [[AVCaptureVideoDataOutput alloc] init];
        NSMutableDictionary *settings = [@{
            (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
        } mutableCopy];
        if (width > 0 && height > 0) {
            // CoreVideo scales the output buffers; when this matches the
            // device's native format (the Miniscope case) it is a no-op.
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
            return false;
        }
        [session addOutput:output];
        [session startRunning];

        m_impl = new Impl;
        m_impl->session = session;
        m_impl->sink = sink;
        m_impl->queue = queue;
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
    sink->latest.copyTo(frame);
    m_impl->consumedSequence = sink->sequence;
    return true;
}
