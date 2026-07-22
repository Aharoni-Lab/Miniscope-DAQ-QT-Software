#ifndef UVCREQUEST_H
#define UVCREQUEST_H

#include <QtGlobal>

// Builders for USB Video Class control-interface requests (UVC 1.1 spec,
// section 4.2: "unit and terminal control requests"). Platform-neutral: each
// transport (IOKit on macOS today; anything speaking raw EP0 tomorrow) fills
// its native setup-packet struct from the SetupPacket returned here, so the
// bit layout that addresses a control lives in one tested place.
namespace UVCRequest {

// bmRequestType for class-specific requests addressed to an interface.
constexpr quint8 kRequestTypeSet = 0x21;   // host->device | class | interface
constexpr quint8 kRequestTypeGet = 0xA1;   // device->host | class | interface

// UVC class-specific request codes.
enum RequestCode : quint8 {
    SET_CUR  = 0x01,
    GET_CUR  = 0x81,
    GET_MIN  = 0x82,
    GET_MAX  = 0x83,
    GET_RES  = 0x84,
    GET_LEN  = 0x85,
    GET_INFO = 0x86,
    GET_DEF  = 0x87,
};

// One EP0 control transfer, laid out like the USB setup packet (field names
// mirror the USB spec and IOKit's IOUSBDevRequest).
struct SetupPacket {
    quint8  bmRequestType = 0;
    quint8  bRequest = 0;
    quint16 wValue = 0;     // control selector in the high byte
    quint16 wIndex = 0;     // unit/terminal ID high byte, VC interface number low byte
    quint16 wLength = 0;
};

SetupPacket makeSet(quint8 vcInterfaceNumber, quint8 unitId, quint8 selector,
                    quint16 length);
SetupPacket makeGet(quint8 vcInterfaceNumber, quint8 unitId, quint8 selector,
                    quint16 length, RequestCode code = GET_CUR);

// UVC control payloads are little-endian on the wire.
void encodeLE16(quint16 value, quint8 *buf2);
quint16 decodeLE16(const quint8 *buf2);

} // namespace UVCRequest

#endif // UVCREQUEST_H
