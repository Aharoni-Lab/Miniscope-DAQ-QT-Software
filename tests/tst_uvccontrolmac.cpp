#include <QtTest>

#include "uvccontrolmac.h"

// Hardware-free smoke tests for the IOKit transport: exercise the IOKit
// registry-walking code paths on whatever machine runs them (CI runners have
// no cameras - an empty enumeration is a pass; the real validation runs on the
// bench via tools/uvc-bench-mac.cpp with a Miniscope attached).
class TestUVCControlMac : public QObject
{
    Q_OBJECT

private slots:
    void enumerateRuns();
    void openNonexistentFails();
    void requestsOnClosedFail();
};

void TestUVCControlMac::enumerateRuns()
{
    // Unfiltered: on any physical Mac this returns at least one hub/device;
    // a bare VM may return none. Either way it must not crash or leak handles
    // (run repeatedly to exercise create/release pairing).
    for (int i = 0; i < 3; i++) {
        const auto all = UVCControlMac::enumerate();
        // Filtered query must be a subset of the unfiltered one.
        const auto none = UVCControlMac::enumerate(0xDEAD, 0xBEEF);
        QVERIFY(none.size() <= all.size());
        for (const auto &d : none) {
            QCOMPARE(d.vid, quint16(0xDEAD));
            QCOMPARE(d.pid, quint16(0xBEEF));
        }
    }
}

void TestUVCControlMac::openNonexistentFails()
{
    UVCControlMac ctrl;
    QVERIFY(!ctrl.open(0xDEAD, 0xBEEF));
    QVERIFY(!ctrl.isOpen());
    QVERIFY(!ctrl.lastError().isEmpty());
}

void TestUVCControlMac::requestsOnClosedFail()
{
    UVCControlMac ctrl;
    quint16 v = 0;
    QVERIFY(!ctrl.setCur(2, 0x03, 1));
    QVERIFY(!ctrl.getCur(2, 0x03, &v));
    QCOMPARE(ctrl.lastError(), QStringLiteral("not open"));
}

QTEST_APPLESS_MAIN(TestUVCControlMac)
#include "tst_uvccontrolmac.moc"
