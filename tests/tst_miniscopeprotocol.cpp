#include <QtTest>

#include "miniscopeprotocol.h"

using MiniscopeProtocol::packI2CPacket;
using MiniscopeProtocol::unpackBnoQuaternion;

// The expected values below are hand-computed from the wire format the DAQ
// firmware parses (see miniscopeprotocol.h). If one of these fails after an
// edit, the change would break every Miniscope's device controls.
class TestMiniscopeProtocol : public QObject
{
    Q_OBJECT

private slots:
    void packShortPacket();
    void packSingleBytePacket();
    void packFiveBytePacket();
    void packSixBytePacket();
    void packSixByteSetsAddressLsb();
    void packRejectsEmpty();
    void packRejectsOversized();

    void bnoIdentityQuaternion();
    void bnoNegativeComponents();
    void bnoNormError();
};

void TestMiniscopeProtocol::packShortPacket()
{
    // The TI 913/914 SERDES init packet sent on every Miniscope connect:
    // address 0xC0, register 0x1F, data 0x10. Short form inserts the total
    // length (3) as byte 1.
    const auto cmd = packI2CPacket({0xC0, 0x1F, 0x10});
    QVERIFY(cmd.valid);
    QCOMPARE(cmd.raw, Q_UINT64_C(0x101F03C0));
    QCOMPARE(cmd.words[0], quint16(0x03C0));   // -> CAP_PROP_CONTRAST / PU contrast
    QCOMPARE(cmd.words[1], quint16(0x101F));   // -> CAP_PROP_GAMMA    / PU gamma
    QCOMPARE(cmd.words[2], quint16(0x0000));   // -> CAP_PROP_SHARPNESS / PU sharpness
}

void TestMiniscopeProtocol::packSingleBytePacket()
{
    // Address-only packet: just address + length, no payload bytes.
    const auto cmd = packI2CPacket({0xC0});
    QVERIFY(cmd.valid);
    QCOMPARE(cmd.raw, Q_UINT64_C(0x01C0));
    QCOMPARE(cmd.words[0], quint16(0x01C0));
    QCOMPARE(cmd.words[1], quint16(0));
    QCOMPARE(cmd.words[2], quint16(0));
}

void TestMiniscopeProtocol::packFiveBytePacket()
{
    // Longest short-form packet: the last payload byte lands in the low byte
    // of the third word.
    const auto cmd = packI2CPacket({0xAA, 0x01, 0x02, 0x03, 0x04});
    QVERIFY(cmd.valid);
    QCOMPARE(cmd.raw, Q_UINT64_C(0x0403020105AA));
    QCOMPARE(cmd.words[0], quint16(0x05AA));
    QCOMPARE(cmd.words[1], quint16(0x0201));
    QCOMPARE(cmd.words[2], quint16(0x0403));
}

void TestMiniscopeProtocol::packSixBytePacket()
{
    // Full form: no length byte; payload starts immediately after the address.
    const auto cmd = packI2CPacket({0xB0, 0x05, 0x20, 0x11, 0x22, 0x33});
    QVERIFY(cmd.valid);
    QCOMPARE(cmd.raw, Q_UINT64_C(0x3322112005B1));   // 0xB0 | 0x01 = 0xB1
    QCOMPARE(cmd.words[0], quint16(0x05B1));
    QCOMPARE(cmd.words[1], quint16(0x1120));
    QCOMPARE(cmd.words[2], quint16(0x3322));
}

void TestMiniscopeProtocol::packSixByteSetsAddressLsb()
{
    // The 6-byte form is flagged by forcing the address LSB to 1 (a real I2C
    // write address always has LSB 0); an already-odd address must not change.
    QCOMPARE(packI2CPacket({0xB1, 0, 0, 0, 0, 0}).raw & 0xFF, Q_UINT64_C(0xB1));
    QCOMPARE(packI2CPacket({0xB0, 0, 0, 0, 0, 0}).raw & 0xFF, Q_UINT64_C(0xB1));
    // ...while the short form leaves the address untouched.
    QCOMPARE(packI2CPacket({0xB0, 0x05}).raw & 0xFF, Q_UINT64_C(0xB0));
}

void TestMiniscopeProtocol::packRejectsEmpty()
{
    const auto cmd = packI2CPacket({});
    QVERIFY(!cmd.valid);
    QCOMPARE(cmd.raw, Q_UINT64_C(0));
}

void TestMiniscopeProtocol::packRejectsOversized()
{
    // > 6 bytes has no wire format (no room in three 16-bit controls).
    const auto cmd = packI2CPacket({1, 2, 3, 4, 5, 6, 7});
    QVERIFY(!cmd.valid);
}

void TestMiniscopeProtocol::bnoIdentityQuaternion()
{
    float out[5];
    unpackBnoQuaternion(16384, 0, 0, 0, out);   // 1.0 in Q14
    QCOMPARE(out[0], 1.0f);
    QCOMPARE(out[1], 0.0f);
    QCOMPARE(out[2], 0.0f);
    QCOMPARE(out[3], 0.0f);
    QCOMPARE(out[4], 0.0f);                     // unit norm -> zero error
}

void TestMiniscopeProtocol::bnoNegativeComponents()
{
    float out[5];
    unpackBnoQuaternion(0, -16384, 0, 0, out);
    QCOMPARE(out[0], 0.0f);
    QCOMPARE(out[1], -1.0f);
    QCOMPARE(out[4], 0.0f);
}

void TestMiniscopeProtocol::bnoNormError()
{
    // A corrupted read (here: two full-scale components) must report its
    // distance from unit norm so the caller can filter it out.
    float out[5];
    unpackBnoQuaternion(16384, 16384, 0, 0, out);
    QCOMPARE(out[4], float(std::sqrt(2.0) - 1.0));
}

QTEST_APPLESS_MAIN(TestMiniscopeProtocol)
#include "tst_miniscopeprotocol.moc"
