#include "uvcrequest.h"

namespace UVCRequest {

static SetupPacket make(quint8 bmRequestType, quint8 bRequest,
                        quint8 vcInterfaceNumber, quint8 unitId,
                        quint8 selector, quint16 length)
{
    SetupPacket p;
    p.bmRequestType = bmRequestType;
    p.bRequest = bRequest;
    p.wValue = quint16(selector) << 8;
    p.wIndex = (quint16(unitId) << 8) | vcInterfaceNumber;
    p.wLength = length;
    return p;
}

SetupPacket makeSet(quint8 vcInterfaceNumber, quint8 unitId, quint8 selector,
                    quint16 length)
{
    return make(kRequestTypeSet, SET_CUR, vcInterfaceNumber, unitId, selector, length);
}

SetupPacket makeGet(quint8 vcInterfaceNumber, quint8 unitId, quint8 selector,
                    quint16 length, RequestCode code)
{
    return make(kRequestTypeGet, code, vcInterfaceNumber, unitId, selector, length);
}

void encodeLE16(quint16 value, quint8 *buf2)
{
    buf2[0] = quint8(value & 0xFF);
    buf2[1] = quint8(value >> 8);
}

quint16 decodeLE16(const quint8 *buf2)
{
    return quint16(buf2[0]) | (quint16(buf2[1]) << 8);
}

} // namespace UVCRequest
