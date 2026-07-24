// Pins the shared capture-backend machinery in VideoStreamBase: the per-frame
// commitFrame() block (slot reservation, conversions, control-register reads,
// DAQ counter bookkeeping), the frame-size lock, and the reconnect backoff.
// This logic used to be triplicated across the OpenCV/libuvc/macOS backends
// and was untestable; a stub backend with scripted control reads makes every
// branch reachable without hardware.

#include <QtTest>
#include <QSemaphore>
#include <QAtomicInt>
#include <QSignalSpy>

#include <opencv2/core/core.hpp>

#include "videostreambase.h"

using namespace MiniscopeProtocol;

class StubStream : public VideoStreamBase
{
    Q_OBJECT
public:
    using VideoStreamBase::VideoStreamBase;
    using VideoStreamBase::commitFrame;
    using VideoStreamBase::resetStreamState;
    using VideoStreamBase::runReconnectCycle;

    bool reconnectResult = false;

    // Scripted control reads: per selector, a FIFO of (ok, value) results.
    // Selectors with no script fall back to (defaultOk, defaultValue).
    QMap<quint8, QList<QPair<bool, quint16>>> script;
    bool defaultOk = true;
    quint16 defaultValue = 0;
    int controlReads = 0;

    int connect2Camera(int) override { return 0; }
    void startStream() override {}
    void startRecording() override {}
    void stopRecording() override {}

protected:
    bool writeControlWord(quint8, quint16) override { return true; }
    bool attemptReconnect() override { return reconnectResult; }
    bool readControl(quint8 selector, quint16 *value) override
    {
        controlReads++;
        if (script.contains(selector) && !script[selector].isEmpty()) {
            const auto entry = script[selector].takeFirst();
            *value = entry.second;
            return entry.first;
        }
        *value = defaultValue;
        return defaultOk;
    }
};

class TestVideoStreamBase : public QObject
{
    Q_OBJECT

    static constexpr int kBufSize = 4;

    // Fresh buffer set per test (semaphores cannot be reset in place).
    StubStream *stream = nullptr;
    cv::Mat frameBuf[kBufSize];
    qint64 tsBuf[kBufSize] = {};
    qint64 daqBuf[kBufSize] = {};
    float bnoBuf[kBufSize * 5] = {};
    QSemaphore *freeFrames = nullptr;
    QSemaphore *usedFrames = nullptr;
    QAtomicInt *acqNum = nullptr;
    QAtomicInt *daqNum = nullptr;

    void buildStream(int width = 0, int height = 0, bool withDaqCounter = true)
    {
        delete stream;
        delete freeFrames;
        delete usedFrames;
        delete acqNum;
        delete daqNum;
        stream = new StubStream(nullptr, width, height, 0);
        freeFrames = new QSemaphore;
        usedFrames = new QSemaphore;
        acqNum = new QAtomicInt(0);
        daqNum = new QAtomicInt(0);
        freeFrames->release(kBufSize);
        for (int i = 0; i < kBufSize; i++)
            frameBuf[i] = cv::Mat();
        std::fill(std::begin(tsBuf), std::end(tsBuf), qint64(0));
        std::fill(std::begin(daqBuf), std::end(daqBuf), qint64(0));
        std::fill(std::begin(bnoBuf), std::end(bnoBuf), 0.0f);
        stream->setBufferParameters(frameBuf, tsBuf, bnoBuf, daqBuf, kBufSize,
                                    freeFrames, usedFrames, acqNum,
                                    withDaqCounter ? daqNum : nullptr);
        stream->resetStreamState();
    }

    static cv::Mat bgrFrame(int w = 4, int h = 4, uchar fill = 100)
    {
        return cv::Mat(h, w, CV_8UC3, cv::Scalar(fill, fill, fill));
    }

    // Simulate the consumer so long sequences of commits never exhaust slots.
    void consumeOne()
    {
        QVERIFY(usedFrames->tryAcquire());
        freeFrames->release();
    }

private slots:
    void cleanupTestCase()
    {
        delete stream;
        delete freeFrames;
        delete usedFrames;
        delete acqNum;
        delete daqNum;
    }

    void commitPublishesFrame()
    {
        buildStream();
        QSignalSpy newFrame(stream, &VideoStreamBase::newFrameAvailable);

        stream->commitFrame(bgrFrame(), 12345);

        QCOMPARE(acqNum->loadRelaxed(), 1);
        QCOMPARE(usedFrames->available(), 1);
        QCOMPARE(freeFrames->available(), kBufSize - 1);
        QCOMPARE(tsBuf[0], qint64(12345));
        QCOMPARE(frameBuf[0].channels(), 1);   // default is grayscale conversion
        QCOMPARE(newFrame.count(), 1);
        QCOMPARE(newFrame.at(0).at(1).toInt(), 1);
    }

    void isColorCopiesUnconverted()
    {
        buildStream();
        stream->setIsColor(true);
        stream->commitFrame(bgrFrame(), 1);
        QCOMPARE(frameBuf[0].channels(), 3);
        QCOMPARE(frameBuf[0].at<cv::Vec3b>(0, 0), cv::Vec3b(100, 100, 100));
    }

    void bufferFullDropsWithoutWriting()
    {
        buildStream();
        QVERIFY(freeFrames->tryAcquire(kBufSize));   // consumer owns every slot
        frameBuf[0] = cv::Mat(4, 4, CV_8UC1, cv::Scalar(77));
        tsBuf[0] = 999;
        QSignalSpy messages(stream, &VideoStreamBase::sendMessage);
        QSignalSpy newFrame(stream, &VideoStreamBase::newFrameAvailable);

        stream->commitFrame(bgrFrame(), 12345);

        QCOMPARE(acqNum->loadRelaxed(), 0);
        QCOMPARE(newFrame.count(), 0);
        QCOMPARE(tsBuf[0], qint64(999));                        // slot untouched
        QCOMPARE(frameBuf[0].at<uchar>(0, 0), uchar(77));       // slot untouched
        QVERIFY(messages.count() > 0);
        QVERIFY(messages.at(0).at(0).toString().contains("buffer is full"));
    }

    void ringBufferWrapsAround()
    {
        buildStream();
        for (int i = 0; i < kBufSize + 2; i++) {
            stream->commitFrame(bgrFrame(), 1000 + i);
            consumeOne();
        }
        // Frame kBufSize wrapped into slot 0, frame kBufSize+1 into slot 1.
        QCOMPARE(tsBuf[0], qint64(1000 + kBufSize));
        QCOMPARE(tsBuf[1], qint64(1000 + kBufSize + 1));
        QCOMPARE(acqNum->loadRelaxed(), kBufSize + 2);
    }

    // First successful read seeds the offset so the counter tracks the
    // acquisition count from frame 2 on; the first slot keeps the raw counter
    // value (long-standing behavior all three backends shared).
    void daqCounterOffsetSeeding()
    {
        buildStream();
        stream->script[SEL_CONTRAST] = {{true, 100}, {true, 101}, {true, 102}};
        for (int i = 0; i < 3; i++)
            stream->commitFrame(bgrFrame(), i);
        QCOMPARE(daqBuf[0], qint64(100));
        QCOMPARE(daqBuf[1], qint64(2));
        QCOMPARE(daqBuf[2], qint64(3));
    }

    // A failed counter read must write the -1 sentinel, not fabricate a 0 -
    // and critically must NOT seed the offset (a garbage offset on frame 0
    // used to corrupt the whole recording's counter column).
    void daqCounterFailedReadWritesSentinel()
    {
        buildStream();
        stream->script[SEL_CONTRAST] = {{false, 0}, {true, 200}, {true, 201}};
        for (int i = 0; i < 3; i++)
            stream->commitFrame(bgrFrame(), i);
        QCOMPARE(daqBuf[0], qint64(-1));       // failed read: sentinel
        QCOMPARE(daqBuf[1], qint64(200));      // first SUCCESS seeds (raw value kept)
        QCOMPARE(daqBuf[2], qint64(2));        // tracking from there
    }

    // The counter register is unsigned 16-bit (UVC CONTRAST): raw values
    // above 32767 must not flip negative (they did on the pre-R4 libuvc/mac
    // paths, which read it through a signed cast).
    void daqCounterIsUnsigned()
    {
        buildStream();
        stream->script[SEL_CONTRAST] = {{true, 40000}, {true, 40001}};
        stream->commitFrame(bgrFrame(), 0);
        consumeOne();
        stream->commitFrame(bgrFrame(), 1);
        QCOMPARE(daqBuf[0], qint64(40000));
        QCOMPARE(daqBuf[1], qint64(2));
    }

    // A reconnected device may have power-cycled and reset its hardware
    // counter; the offset re-seeds so the CSV column restarts (raw, 2, 3...)
    // instead of staying skewed by the stale offset for the rest of the file.
    void daqCounterReseedsAfterReconnect()
    {
        buildStream();
        stream->script[SEL_CONTRAST] = {{true, 100}, {true, 101}};
        stream->commitFrame(bgrFrame(), 0);
        consumeOne();
        stream->commitFrame(bgrFrame(), 1);
        consumeOne();
        QCOMPARE(daqBuf[1], qint64(2));

        stream->reconnectResult = true;
        ReconnectBackoff backoff;
        QVERIFY(stream->runReconnectCycle(backoff, "grab frame"));   // ~1 s (first backoff delay)

        stream->script[SEL_CONTRAST] = {{true, 5}, {true, 6}};      // counter reset by power-cycle
        stream->commitFrame(bgrFrame(), 2);
        consumeOne();
        stream->commitFrame(bgrFrame(), 3);
        QCOMPARE(daqBuf[2], qint64(5));   // fresh seed: raw value, like at stream start
        QCOMPARE(daqBuf[3], qint64(2));
    }

    void noDaqCounterMeansSentinel()
    {
        buildStream(0, 0, /*withDaqCounter=*/false);
        stream->commitFrame(bgrFrame(), 1);
        QCOMPARE(daqBuf[0], qint64(-1));
        QCOMPARE(stream->controlReads, 0);   // no counter -> no register traffic
    }

    void extTriggerEmitsOnEdges()
    {
        buildStream();
        stream->setExtTriggerTrackingState(true);
        stream->script[SEL_GAMMA] = {{true, 0}, {true, 0}, {true, 1}, {true, 1}, {true, 0}};
        QSignalSpy triggers(stream, &VideoStreamBase::extTriggered);
        for (int i = 0; i < 5; i++) {
            stream->commitFrame(bgrFrame(), i);
            consumeOne();
        }
        QCOMPARE(triggers.count(), 2);
        QCOMPARE(triggers.at(0).at(0).toBool(), true);    // 0 -> 1 rising
        QCOMPARE(triggers.at(1).at(0).toBool(), false);   // 1 -> 0 falling
    }

    // A failed trigger read is skipped entirely: treating it as 0 would fake
    // a falling edge (and the rebound a rising one).
    void extTriggerFailedReadFakesNoEdge()
    {
        buildStream();
        stream->setExtTriggerTrackingState(true);
        stream->script[SEL_GAMMA] = {{true, 1}, {false, 0}, {true, 1}};
        QSignalSpy triggers(stream, &VideoStreamBase::extTriggered);
        for (int i = 0; i < 3; i++) {
            stream->commitFrame(bgrFrame(), i);
            consumeOne();
        }
        QCOMPARE(triggers.count(), 0);
    }

    void bnoUnpacksIntoSlot()
    {
        buildStream();
        stream->setHeadOrientationConfig(true, false);
        // Unit quaternion w=1: raw w = 2^14, x=y=z=0.
        stream->script[SEL_SATURATION] = {{true, 16384}};
        stream->commitFrame(bgrFrame(), 1);
        QCOMPARE(bnoBuf[0], 1.0f);                       // w
        QCOMPARE(bnoBuf[1], 0.0f);                       // x
        QCOMPARE(bnoBuf[2], 0.0f);                       // y
        QCOMPARE(bnoBuf[3], 0.0f);                       // z
        QVERIFY(qAbs(bnoBuf[4]) < 1e-5f);                // normError ~ 0
    }

    // Failed BNO reads keep the last good quaternion instead of fabricating
    // an all-zero one; before any good read, the default is flagged corrupt
    // via normError = 1 so consumers reject it.
    void bnoFailedReadKeepsLastGood()
    {
        buildStream();
        stream->setHeadOrientationConfig(true, false);
        stream->script[SEL_SATURATION] = {{false, 0}, {true, 16384}, {false, 0}};
        for (int i = 0; i < 3; i++) {
            stream->commitFrame(bgrFrame(), i);
            consumeOne();
        }
        QCOMPARE(bnoBuf[0 * 5 + 0], 0.0f);   // before first success: zero quat...
        QCOMPARE(bnoBuf[0 * 5 + 4], 1.0f);   // ...flagged corrupt
        QCOMPARE(bnoBuf[1 * 5 + 0], 1.0f);   // good read
        QCOMPARE(bnoBuf[2 * 5 + 0], 1.0f);   // failed read keeps last good
        QVERIFY(qAbs(bnoBuf[2 * 5 + 4]) < 1e-5f);
    }

    void frameSizeLocksToFirstFrame()
    {
        buildStream();
        QSignalSpy messages(stream, &VideoStreamBase::sendMessage);
        QSignalSpy newFrame(stream, &VideoStreamBase::newFrameAvailable);

        stream->commitFrame(bgrFrame(4, 4), 1);
        stream->commitFrame(bgrFrame(8, 8), 2);   // mid-stream size change: dropped
        stream->commitFrame(bgrFrame(4, 4), 3);   // matching frames keep flowing

        QCOMPARE(newFrame.count(), 2);
        QCOMPARE(acqNum->loadRelaxed(), 2);
        QCOMPARE(messages.count(), 1);
        QVERIFY(messages.at(0).at(0).toString().contains("mid-stream"));
    }

    void mismatchWarningOncePerEpisode()
    {
        buildStream();
        QSignalSpy messages(stream, &VideoStreamBase::sendMessage);
        stream->commitFrame(bgrFrame(4, 4), 1);
        stream->commitFrame(bgrFrame(8, 8), 2);
        stream->commitFrame(bgrFrame(8, 8), 3);   // same episode: quiet drop
        QCOMPARE(messages.count(), 1);
        stream->commitFrame(bgrFrame(4, 4), 4);   // recovery resets the warning
        stream->commitFrame(bgrFrame(8, 8), 5);   // new episode warns again
        QCOMPARE(messages.count(), 2);
    }

    void configSizeMismatchWarnsButCommits()
    {
        buildStream(10, 10);
        QSignalSpy messages(stream, &VideoStreamBase::sendMessage);
        stream->commitFrame(bgrFrame(4, 4), 1);
        QCOMPARE(acqNum->loadRelaxed(), 1);   // still committed
        QCOMPARE(messages.count(), 1);
        QVERIFY(messages.at(0).at(0).toString().contains("config expects"));
    }

    void backoffProgression()
    {
        ReconnectBackoff backoff;
        QVERIFY(backoff.firstFailure());
        QCOMPARE(backoff.nextDelayMs(), 1000);
        QVERIFY(!backoff.firstFailure());
        QCOMPARE(backoff.nextDelayMs(), 2000);
        QCOMPARE(backoff.nextDelayMs(), 3000);
        QCOMPARE(backoff.nextDelayMs(), 4000);
        QCOMPARE(backoff.nextDelayMs(), 5000);
        QCOMPARE(backoff.nextDelayMs(), 5000);   // capped
        QCOMPARE(backoff.attempts(), 6);
        backoff.reset();
        QVERIFY(backoff.firstFailure());
        QCOMPARE(backoff.nextDelayMs(), 1000);
    }
};

QTEST_GUILESS_MAIN(TestVideoStreamBase)
#include "tst_videostreambase.moc"
