#include <QtTest>

#include "miniscopeprotocol.h"
#include "uvcrequest.h"

using namespace UVCRequest;

// Expected values hand-computed from UVC 1.1 section 4.2: wValue carries the
// control selector in its high byte, wIndex the unit ID (high byte) and the
// VideoControl interface number (low byte). A mistake here addresses the wrong
// control - or the wrong unit - on every platform that uses this builder.
class TestUVCRequest : public QObject
{
    Q_OBJECT

private slots:
    void setCurLayout();
    void getCurLayout();
    void getVariants();
    void nonzeroInterfaceNumber();
    void le16Roundtrip();
    void le16NegativeAsSigned();
};

void TestUVCRequest::setCurLayout()
{
    // The exact request the Miniscope backends issue for the I2C low word:
    // SET_CUR of PU contrast (selector 0x03) on processing unit 2, interface 0.
    const auto p = makeSet(0, MiniscopeProtocol::kProcessingUnitId,
                           MiniscopeProtocol::SEL_CONTRAST, 2);
    QCOMPARE(p.bmRequestType, quint8(0x21));   // host->device | class | interface
    QCOMPARE(p.bRequest, quint8(0x01));        // SET_CUR
    QCOMPARE(p.wValue, quint16(0x0300));
    QCOMPARE(p.wIndex, quint16(0x0200));
    QCOMPARE(p.wLength, quint16(2));
}

void TestUVCRequest::getCurLayout()
{
    // The per-frame frame-counter read.
    const auto p = makeGet(0, MiniscopeProtocol::kProcessingUnitId,
                           MiniscopeProtocol::SEL_CONTRAST, 2);
    QCOMPARE(p.bmRequestType, quint8(0xA1));   // device->host | class | interface
    QCOMPARE(p.bRequest, quint8(0x81));        // GET_CUR
    QCOMPARE(p.wValue, quint16(0x0300));
    QCOMPARE(p.wIndex, quint16(0x0200));
    QCOMPARE(p.wLength, quint16(2));
}

void TestUVCRequest::getVariants()
{
    QCOMPARE(makeGet(0, 2, 0x03, 2, GET_MIN).bRequest, quint8(0x82));
    QCOMPARE(makeGet(0, 2, 0x03, 2, GET_MAX).bRequest, quint8(0x83));
    QCOMPARE(makeGet(0, 2, 0x03, 2, GET_INFO).bRequest, quint8(0x86));
}

void TestUVCRequest::nonzeroInterfaceNumber()
{
    // Devices whose VideoControl interface is not interface 0 must see their
    // real interface number in wIndex's low byte.
    const auto p = makeSet(3, 5, MiniscopeProtocol::SEL_GAMMA, 2);
    QCOMPARE(p.wValue, quint16(0x0900));
    QCOMPARE(p.wIndex, quint16(0x0503));
}

void TestUVCRequest::le16Roundtrip()
{
    quint8 buf[2];
    encodeLE16(0x1234, buf);
    QCOMPARE(buf[0], quint8(0x34));   // low byte first on the wire
    QCOMPARE(buf[1], quint8(0x12));
    QCOMPARE(decodeLE16(buf), quint16(0x1234));
}

void TestUVCRequest::le16NegativeAsSigned()
{
    // BNO quaternion components are signed; a full-scale negative value must
    // survive the trip through the unsigned wire encoding.
    quint8 buf[2];
    encodeLE16(quint16(qint16(-16384)), buf);
    QCOMPARE(qint16(decodeLE16(buf)), qint16(-16384));
}

QTEST_APPLESS_MAIN(TestUVCRequest)
#include "tst_uvcrequest.moc"
