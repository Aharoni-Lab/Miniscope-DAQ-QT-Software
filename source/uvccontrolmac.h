#ifndef UVCCONTROLMAC_H
#define UVCCONTROLMAC_H

// UVC control transport for macOS: sends SET_CUR / GET_CUR requests to a UVC
// camera over the default control pipe (EP0) while the system streams from it.
//
// Why this exists (and why not libuvc): since macOS 12, Apple's UVC driver
// stack claims every UVC device exclusively, so libusb/libuvc cannot claim the
// interfaces without running as root. Apple's driver does, however, leave EP0
// available - UVC class requests are interface-addressed control transfers
// that ride the default pipe, and Apple DTS explicitly endorses sending them
// (and polling status) from a normal user process while the device streams.
//
// The one load-bearing rule, learned from openpnp-capture's macOS fix: NEVER
// call USBInterfaceOpen() on the VideoControl interface (that needs exclusive
// access and fails with kIOReturnExclusiveAccess while Apple's driver holds
// the device). IOKit permits ControlRequest() on pipe 0 of an *un-opened*
// IOUSBInterfaceInterface, which is exactly what this class does.
//
// Threading: not thread-safe; callers serialize requests on one thread (the
// capture backends already send all control traffic from the stream thread).
// The DAQ's control endpoint is slow to clear, so a settle delay is applied
// after every transfer (same reason as the OpenCV/libuvc backends' waits).

#include <QString>
#include <QVector>
#include <QtGlobal>

class UVCControlMac
{
public:
    struct DeviceInfo {
        quint16 vid = 0;
        quint16 pid = 0;
        quint32 locationID = 0;   // stable per physical port; disambiguates identical devices
        QString name;             // USB product string, when the device provides one
    };

    // All connected USB devices matching vid/pid (0 = match any). Contains every
    // USB device, not only cameras, when called with (0, 0).
    static QVector<DeviceInfo> enumerate(quint16 vid = 0, quint16 pid = 0);

    UVCControlMac() = default;
    ~UVCControlMac();
    UVCControlMac(const UVCControlMac &) = delete;
    UVCControlMac &operator=(const UVCControlMac &) = delete;

    // Find the device by VID/PID (plus locationID when there are several) and
    // get a handle on its VideoControl interface. Does not disturb streaming.
    bool open(quint16 vid, quint16 pid, quint32 locationID = 0);
    void close();
    bool isOpen() const { return m_intf != nullptr; }

    // UVC SET_CUR / GET_CUR of a 16-bit control value on the given unit.
    bool setCur(quint8 unitId, quint8 selector, quint16 value);
    bool getCur(quint8 unitId, quint8 selector, quint16 *value);

    quint8 vcInterfaceNumber() const { return m_vcInterfaceNumber; }
    quint32 locationID() const { return m_locationID; }
    QString lastError() const { return m_lastError; }

private:
    bool controlRequest(quint8 bmRequestType, quint8 bRequest, quint16 wValue,
                        quint16 wIndex, quint16 wLength, void *data);

    // IOUSBInterfaceInterface** for the VideoControl interface; kept opaque so
    // IOKit headers stay out of this header (they are C typedefs of anonymous
    // structs and cannot be forward-declared).
    void *m_intf = nullptr;
    quint8 m_vcInterfaceNumber = 0;
    quint32 m_locationID = 0;
    QString m_lastError;
};

#endif // UVCCONTROLMAC_H
