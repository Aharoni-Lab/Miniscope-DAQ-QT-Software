#include <QtTest>

#include <opencv2/core/core.hpp>

#include "avfframegrabbermac.h"

// Hardware-free smoke tests for the uniqueID-pinned frame grabber: the
// failure paths must fail cleanly (message, no crash), because the stream
// loop leans on them for its reconnect handling.
class TestAvfFrameGrabber : public QObject
{
    Q_OBJECT

private slots:
    void openRejectsUnknownUniqueId();
    void readWithoutOpenFails();
    void releaseWithoutOpenIsSafe();
};

void TestAvfFrameGrabber::openRejectsUnknownUniqueId()
{
    AvfFrameGrabber grabber;
    QVERIFY(!grabber.open(QStringLiteral("0xdeadbeef00000000"), 608, 608));
    QVERIFY(grabber.lastError().contains(QStringLiteral("no capture device")));
    QVERIFY(!grabber.isOpened());
}

void TestAvfFrameGrabber::readWithoutOpenFails()
{
    AvfFrameGrabber grabber;
    cv::Mat frame;
    QVERIFY(!grabber.read(frame, 10));
    QVERIFY(!grabber.lastError().isEmpty());
}

void TestAvfFrameGrabber::releaseWithoutOpenIsSafe()
{
    AvfFrameGrabber grabber;
    grabber.release();
    grabber.release();   // double-release must also be a no-op
    QVERIFY(!grabber.isOpened());
}

QTEST_MAIN(TestAvfFrameGrabber)
#include "tst_avfframegrabber.moc"
