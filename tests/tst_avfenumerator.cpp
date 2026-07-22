#include <QtTest>

#include "avfenumeratormac.h"

// parseAvfUsbUniqueId turns an AVFoundation uniqueID back into the USB
// identity that UVCControlMac opens; a wrong split here means the app opens
// the control channel of the WRONG Miniscope on multi-scope rigs.
class TestAvfEnumerator : public QObject
{
    Q_OBJECT

private slots:
    void parseUsbUniqueId();
    void parseWithoutPrefixOrLeadingZeros();
    void parseRejectsOpaqueIds();
    void enumerateRuns();
};

void TestAvfEnumerator::parseUsbUniqueId()
{
    // locationID 0x00100000, VID 0x05a3, PID 0x9331 (the webcam used for
    // hardware validation of the transport).
    quint32 loc = 0; quint16 vid = 0, pid = 0;
    QVERIFY(parseAvfUsbUniqueId(QStringLiteral("0x0010000005a39331"), &loc, &vid, &pid));
    QCOMPARE(loc, quint32(0x00100000));
    QCOMPARE(vid, quint16(0x05a3));
    QCOMPARE(pid, quint16(0x9331));
}

void TestAvfEnumerator::parseWithoutPrefixOrLeadingZeros()
{
    // AVFoundation formats the 64-bit value as hex; be tolerant of a missing
    // "0x" and of dropped leading zeros.
    quint32 loc = 0; quint16 vid = 0, pid = 0;
    QVERIFY(parseAvfUsbUniqueId(QStringLiteral("1410000004b400f9"), &loc, &vid, &pid));
    QCOMPARE(loc, quint32(0x14100000));
    QCOMPARE(vid, quint16(0x04b4));   // the Miniscope DAQ
    QCOMPARE(pid, quint16(0x00f9));
}

void TestAvfEnumerator::parseRejectsOpaqueIds()
{
    quint32 loc = 0; quint16 vid = 0, pid = 0;
    // Built-in camera on Apple Silicon: an opaque UUID, not a USB identity.
    QVERIFY(!parseAvfUsbUniqueId(QStringLiteral("1F06D5FF-4C76-410E-9770-C04C467C1317"),
                                 &loc, &vid, &pid));
    QVERIFY(!parseAvfUsbUniqueId(QString(), &loc, &vid, &pid));
    QVERIFY(!parseAvfUsbUniqueId(QStringLiteral("0x"), &loc, &vid, &pid));
    // Parses as hex but has no location/vendor - not a USB camera.
    QVERIFY(!parseAvfUsbUniqueId(QStringLiteral("0x1234"), &loc, &vid, &pid));
}

void TestAvfEnumerator::enumerateRuns()
{
    // Hardware-free smoke test: must not crash; every USB-flagged entry must
    // carry a plausible identity. Logs what it saw for bench-day eyeballing.
    const auto cameras = enumerateAvfCameras();
    for (const auto &cam : cameras) {
        qInfo().nospace() << "camera: " << cam.name << " uniqueID=" << cam.uniqueID
                          << (cam.isUsb ? " (USB)" : " (non-USB)");
        if (cam.isUsb) {
            QVERIFY(cam.locationID != 0);
            QVERIFY(cam.vid != 0);
        }
    }
}

QTEST_MAIN(TestAvfEnumerator)
#include "tst_avfenumerator.moc"
