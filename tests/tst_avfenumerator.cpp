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
    void resolveUsesIndexLocation();
    void resolveWarnsOnUnexpectedIdentity();
    void resolveFallsBackToOnlyMiniscope();
    void resolveRefusesToGuessAmongSeveral();
    void resolveFailsWithNoneAttached();
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

// --- resolveControlTarget: which USB device the control channel opens --------
// The multi-scope failure mode this guards against: two Miniscopes share
// VID/PID, so a wrong decision here streams frames from one scope while LED /
// gain commands silently drive the other.

static const quint16 kVid = 0x04b4, kPid = 0x00f9;   // Miniscope DAQ (Cypress FX3)

static AvfCameraInfo usbCam(const QString &name, quint32 loc, quint16 vid, quint16 pid)
{
    AvfCameraInfo cam;
    cam.name = name;
    cam.locationID = loc;
    cam.vid = vid;
    cam.pid = pid;
    cam.isUsb = true;
    return cam;
}

void TestAvfEnumerator::resolveUsesIndexLocation()
{
    // Two Miniscopes attached; the index resolves, so each deviceID must map
    // to its own locationID - never "the first one found".
    const QVector<AvfCameraInfo> cams = {usbCam("Miniscope A", 0x00100000, kVid, kPid),
                                         usbCam("Miniscope B", 0x00200000, kVid, kPid)};
    const QVector<quint32> attached = {0x00100000, 0x00200000};

    const auto a = resolveControlTarget(cams, 0, kVid, kPid, attached);
    QVERIFY(a.ok);
    QCOMPARE(a.locationID, quint32(0x00100000));
    QVERIFY(a.warning.isEmpty());

    const auto b = resolveControlTarget(cams, 1, kVid, kPid, attached);
    QVERIFY(b.ok);
    QCOMPARE(b.locationID, quint32(0x00200000));
}

void TestAvfEnumerator::resolveWarnsOnUnexpectedIdentity()
{
    // The config points at a resolvable USB camera that isn't a Miniscope:
    // proceed (it may be a behavior cam misconfigured as a Miniscope - the
    // open will fail cleanly) but tell the user what it looked like.
    const QVector<AvfCameraInfo> cams = {usbCam("HD Web Camera", 0x00100000, 0x05a3, 0x9331)};
    const auto t = resolveControlTarget(cams, 0, kVid, kPid, {});
    QVERIFY(t.ok);
    QCOMPARE(t.locationID, quint32(0x00100000));
    QVERIFY(t.warning.contains(QStringLiteral("does not look like a Miniscope")));
}

void TestAvfEnumerator::resolveFallsBackToOnlyMiniscope()
{
    // Index unresolvable (opaque uniqueID -> isUsb=false), but exactly one
    // Miniscope is attached: unambiguous, use it (with a warning).
    AvfCameraInfo builtin;
    builtin.name = QStringLiteral("FaceTime HD Camera");
    const auto t = resolveControlTarget({builtin}, 0, kVid, kPid, {0x00300000});
    QVERIFY(t.ok);
    QCOMPARE(t.locationID, quint32(0x00300000));
    QVERIFY(t.warning.contains(QStringLiteral("only Miniscope")));

    // Same for an out-of-range index (stale deviceID in the config).
    const auto o = resolveControlTarget({builtin}, 5, kVid, kPid, {0x00300000});
    QVERIFY(o.ok);
    QCOMPARE(o.locationID, quint32(0x00300000));
}

void TestAvfEnumerator::resolveRefusesToGuessAmongSeveral()
{
    // Index unresolvable AND several Miniscopes attached: must fail loudly,
    // never coin-flip the control channel onto one of them.
    AvfCameraInfo builtin;
    const auto t = resolveControlTarget({builtin}, 0, kVid, kPid,
                                        {0x00100000, 0x00200000});
    QVERIFY(!t.ok);
    QVERIFY(t.error.contains(QStringLiteral("refusing to guess")));
}

void TestAvfEnumerator::resolveFailsWithNoneAttached()
{
    const auto t = resolveControlTarget({}, 0, kVid, kPid, {});
    QVERIFY(!t.ok);
    QVERIFY(t.error.contains(QStringLiteral("no Miniscope DAQ")));
}

QTEST_MAIN(TestAvfEnumerator)
#include "tst_avfenumerator.moc"
