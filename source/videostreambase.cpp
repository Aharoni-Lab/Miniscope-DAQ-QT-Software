#include "videostreambase.h"

#include <QAtomicInt>
#include <QCoreApplication>
#include <QDebug>
#include <QSemaphore>
#include <QThread>

#include <opencv2/imgproc.hpp>

using namespace MiniscopeProtocol;

Q_LOGGING_CATEGORY(msDiag, "miniscope.diag")

VideoStreamBase::VideoStreamBase(QObject *parent, int width, int height, double pixelClock) :
    QObject(parent),
    m_expectedWidth(width),
    m_expectedHeight(height),
    m_pixelClock(pixelClock)
{
    resetStreamState();
}

void VideoStreamBase::setBufferParameters(cv::Mat *frameBuf, qint64 *tsBuf, float *bnoBuf,
                                          qint64 *daqFrameNumBuf,
                                          int bufferSize, QSemaphore *freeFramesS, QSemaphore *usedFramesS,
                                          QAtomicInt *acqFrameNum, QAtomicInt *daqFrameNumber)
{
    frameBuffer = frameBuf;
    timeStampBuffer = tsBuf;
    daqFrameNumBuffer = daqFrameNumBuf;
    bnoBuffer = bnoBuf;
    frameBufferSize = bufferSize;
    freeFrames = freeFramesS;
    usedFrames = usedFramesS;
    m_acqFrameNum = acqFrameNum;
    daqFrameNum = daqFrameNumber;
}

int VideoStreamBase::connect2Video(QString, QString, float)
{
    sendMessage("Error: video playback is not supported by this capture backend.");
    return 0;
}

void VideoStreamBase::convertToSlot(const cv::Mat &frame, cv::Mat &slot)
{
    if (m_isColor)
        frame.copyTo(slot);
    else
        cv::cvtColor(frame, slot, cv::COLOR_BGR2GRAY);
}

void VideoStreamBase::onFrameCommitted(int streamIdx, const cv::Mat &frame, qint64 timestampMs)
{
    // Heartbeat every 100 frames: proves the loop is alive in lab logs and
    // gives a per-frame-rate DAQ counter sample without any extra register
    // traffic. Skipped for devices with no counter (video playback, webcams
    // without a control channel).
    if (streamIdx % 100 != 0 || daqFrameNum == nullptr)
        return;
    qCInfo(msDiag).nospace()
        << m_deviceName << (streamIdx == 0 ? " first frame: " : " heartbeat: ")
        << frame.cols << "x" << frame.rows << " acqFrame=" << streamIdx
        << " daqFrameCounter=" << (daqFrameNum->loadRelaxed() + m_daqFrameNumOffset)
        << " ts=" << timestampMs;
}

void VideoStreamBase::sendCommands()
{
    if (!m_commandQueue.flush([this](quint8 sel, quint16 word) { return writeControlWord(sel, word); }))
        qDebug() << "Send setting failed";
}

void VideoStreamBase::sendSerdesModeCommands()
{
    const auto packets = serdesModePackets(m_pixelClock);
    if (packets.isEmpty())
        return;
    // The SERDES mode must be the FIRST traffic after a (re)connect. Drop
    // anything still queued - e.g. control changes made while the device was
    // down, which would otherwise flush ahead of these packets - the
    // requestInitCommands() that follows a reconnect restores device state.
    m_commandQueue.clear();
    for (int i = 0; i < packets.size(); i++)
        setPropertyI2C(i, packets[i]);
    sendCommands();
    QThread::msleep(500);
}

void VideoStreamBase::resetStreamState()
{
    m_stopStreaming = false;
    m_streamIdx = 0;
    m_daqFrameNumOffset = 0;
    m_daqOffsetSeeded = false;
    m_extTriggerLast = -1;
    m_lockedFrameSize = cv::Size();
    m_sizeMismatchWarned = false;
    // Zero quaternion with normError 1: frames committed before the first
    // successful BNO read are flagged corrupted, not passed off as valid.
    m_lastBnoQuat[0] = m_lastBnoQuat[1] = m_lastBnoQuat[2] = m_lastBnoQuat[3] = 0.0f;
    m_lastBnoQuat[4] = 1.0f;
}

void VideoStreamBase::commitFrame(const cv::Mat &frame, qint64 timestampMs)
{
    // The ring buffer's consumers (DataSaver, the display path) assume every
    // slot has the same geometry, so the stream is locked to the size of the
    // first frame it delivers. A mid-stream size change (e.g. a reconnect
    // that renegotiated a different resolution) drops frames loudly instead
    // of corrupting the recording.
    if (m_lockedFrameSize.empty()) {
        m_lockedFrameSize = frame.size();
        if ((m_expectedWidth > 0 && frame.cols != m_expectedWidth) ||
            (m_expectedHeight > 0 && frame.rows != m_expectedHeight))
            sendMessage("Warning: " + m_deviceName + " delivers " +
                        QString::number(frame.cols) + "x" + QString::number(frame.rows) +
                        " but the config expects " +
                        QString::number(m_expectedWidth) + "x" + QString::number(m_expectedHeight) +
                        ". Using the delivered size.");
    } else if (frame.size() != m_lockedFrameSize) {
        if (!m_sizeMismatchWarned) {
            sendMessage("Error: " + m_deviceName + " delivered a " +
                        QString::number(frame.cols) + "x" + QString::number(frame.rows) +
                        " frame mid-stream (stream is " +
                        QString::number(m_lockedFrameSize.width) + "x" +
                        QString::number(m_lockedFrameSize.height) +
                        "). Dropping mismatched frames.");
            m_sizeMismatchWarned = true;
        }
        return;
    } else {
        m_sizeMismatchWarned = false;
    }

    // Reserve the ring-buffer slot BEFORE any per-frame work: when the buffer
    // is full, m_streamIdx % frameBufferSize is the oldest unconsumed slot,
    // which DataSaver may be reading right now - writing first corrupts the
    // frame being saved. Acquiring first also skips the per-frame control
    // reads (~6 ms each on some transports) for frames we are about to drop.
    if (!freeFrames->tryAcquire()) {
        // No free slot: this frame is thrown away.
        if (freeFrames->available() == 0) {
            sendMessage("Error: " + m_deviceName + " frame buffer is full. Frames will be lost!");
            QThread::msleep(100);
        }
        return;
    }

    const int bufIdx = m_streamIdx % frameBufferSize;
    timeStampBuffer[bufIdx] = timestampMs;
    convertToSlot(frame, frameBuffer[bufIdx]);

    if (m_trackExtTrigger) {
        quint16 trigger = 0;
        // A failed read is skipped entirely - treating it as 0 would fake a
        // trigger edge.
        if (readControl(SEL_GAMMA, &trigger)) {
            if (m_extTriggerLast < 0) {
                m_extTriggerLast = trigger;   // first read only primes the edge detector
            } else if (m_extTriggerLast != trigger) {
                emit extTriggered(m_extTriggerLast == 0);
                m_extTriggerLast = trigger;
            }
        }
    }

    if (m_headOrientationStreamState) {
        // BNO output is a unit quaternion after a 2^14 division.
        quint16 quat[4];   // w, x, y, z per kBnoSelectors order
        bool ok = true;
        for (int i = 0; i < 4 && ok; i++)
            ok = readControl(kBnoSelectors[i], &quat[i]);
        if (ok)
            unpackBnoQuaternion(qint16(quat[0]), qint16(quat[1]),
                                qint16(quat[2]), qint16(quat[3]), m_lastBnoQuat);
        // Failed reads keep the last good quaternion rather than fabricating
        // an all-zero one.
        for (int i = 0; i < 5; i++)
            bnoBuffer[bufIdx * 5 + i] = m_lastBnoQuat[i];
    }

    bool daqCounterValid = false;
    if (daqFrameNum != nullptr) {
        quint16 counter = 0;
        if (readControl(SEL_CONTRAST, &counter)) {
            daqCounterValid = true;
            // The counter is an UNSIGNED 16-bit register (UVC CONTRAST is
            // unsigned, unlike the signed BNO registers) - the historical
            // OpenCV/Windows behavior. Interpreting it as signed would flip
            // recorded values negative halfway through the wrap period.
            *daqFrameNum = int(counter) - m_daqFrameNumOffset;
            if (!m_daqOffsetSeeded) {
                // Sync the DAQ counter to the acquisition count on the first
                // SUCCESSFUL read (not blindly on frame 0: a failed read there
                // would poison the offset for the whole recording).
                m_daqFrameNumOffset = *daqFrameNum - 1;
                m_daqOffsetSeeded = true;
                // Remember where the acquisition count stood, so the live
                // dropped-frame readout can be measured within this connection
                // epoch (see droppedFrameEstimate()). 0 at stream start; the
                // current (large) count after a reconnect.
                m_acqAtDaqSeed = m_acqFrameNum->loadRelaxed();
            }
        }
        // On failure *daqFrameNum keeps its last value; the CSV gets -1 below.
    }
    if (daqFrameNumBuffer != nullptr)
        daqFrameNumBuffer[bufIdx] = (daqFrameNum != nullptr && daqCounterValid)
                                        ? qint64(daqFrameNum->loadRelaxed())
                                        : qint64(-1);

    m_acqFrameNum->operator++();
    const int committedIdx = m_streamIdx;
    m_streamIdx++;
    emit newFrameAvailable(m_deviceName, *m_acqFrameNum);
    usedFrames->release();

    onFrameCommitted(committedIdx, frame, timestampMs);
}

bool VideoStreamBase::runReconnectCycle(ReconnectBackoff &backoff, const QString &what)
{
    if (backoff.firstFailure()) {
        sendMessage("Warning: " + m_deviceName + " " + what + " failed. Attempting to reconnect.");
        diagnoseStreamFailure();
    }
    // Interruptible backoff: sleep in short slices, delivering queued events
    // (so a window-close stopStream() lands) and checking the stop flag each
    // slice. One blind 5 s sleep would outlive stopAndJoinStream()'s 3 s join
    // timeout, leaving this thread to wake and write into buffers the GUI
    // thread may already have freed during shutdown.
    const int delayMs = backoff.nextDelayMs();
    for (int slept = 0; slept < delayMs; slept += 100) {
        QThread::msleep(100);
        QCoreApplication::processEvents();
        if (m_stopStreaming)
            return false;
    }
    if (attemptReconnect()) {
        sendMessage("Warning: " + m_deviceName + " reconnected (after " +
                    QString::number(backoff.attempts()) + " attempts).");
        qDebug() << "Reconnect to camera" << m_cameraID;
        backoff.reset();
        // The device may have power-cycled, resetting its hardware frame
        // counter - a stale offset would leave the CSV's DAQ Frame Number
        // column nonsensical (e.g. negative) for the rest of the recording.
        // Re-seed on the next successful read, exactly like at stream start:
        // the column restarts at the raw value then counts 2, 3, ... and the
        // reset stays visible post-hoc at the reconnect boundary.
        m_daqOffsetSeeded = false;
        m_daqFrameNumOffset = 0;
        return true;
    }
    return false;
}

void VideoStreamBase::serviceCommandQueue()
{
    // Deliver queued cross-thread slot calls (setPropertyI2C, stopStream when
    // signalled) that arrived while this loop iteration ran.
    QCoreApplication::processEvents();
    if (!m_commandQueue.isEmpty())
        sendCommands();
}
