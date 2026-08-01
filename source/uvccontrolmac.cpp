#include "uvccontrolmac.h"

#include <QDebug>
#include <QThread>

#include <functional>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USBSpec.h>

// The interface typedef behind the opaque handle in the header.
using IntfHandle = IOUSBInterfaceInterface **;

static QString ioReturnToString(IOReturn kr)
{
    switch (kr) {
    case kIOReturnSuccess:         return QStringLiteral("success");
    case kIOReturnExclusiveAccess: return QStringLiteral("exclusive access (another driver holds it)");
    case kIOReturnNotResponding:   return QStringLiteral("device not responding");
    case kIOReturnNoDevice:        return QStringLiteral("device gone");
    case kIOReturnAborted:         return QStringLiteral("transfer aborted");
    case kIOUSBPipeStalled:        return QStringLiteral("pipe stalled");
    default:
        return QStringLiteral("IOReturn 0x") + QString::number(quint32(kr), 16);
    }
}

// Read a numeric registry property (idVendor / idProduct / locationID);
// 0 when absent, which no USB VID/PID/location legitimately is.
static quint32 numberProperty(io_service_t service, CFStringRef key)
{
    quint32 out = 0;
    CFTypeRef ref = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    if (ref) {
        if (CFGetTypeID(ref) == CFNumberGetTypeID())
            CFNumberGetValue((CFNumberRef)ref, kCFNumberSInt32Type, &out);
        CFRelease(ref);
    }
    return out;
}

static QString stringProperty(io_service_t service, CFStringRef key)
{
    QString out;
    CFTypeRef ref = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    if (ref) {
        if (CFGetTypeID(ref) == CFStringGetTypeID())
            out = QString::fromCFString((CFStringRef)ref);
        CFRelease(ref);
    }
    return out;
}

// Iterate USB devices in the IO registry, invoking fn(service, vid, pid,
// locationID). kIOUSBDeviceClassName matches modern registrations too (it is
// a compatibility alias for IOUSBHostDevice on every macOS this code targets,
// macOS 12+). The walk is a few dozen entries, so no early-stop plumbing.
static void forEachUsbDevice(
    const std::function<void(io_service_t, quint16, quint16, quint32)> &fn)
{
    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    if (!match)
        return;
    io_iterator_t iter = IO_OBJECT_NULL;
    // Consumes 'match'.
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter) != KERN_SUCCESS)
        return;
    io_service_t service;
    while ((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        fn(service,
           quint16(numberProperty(service, CFSTR("idVendor"))),
           quint16(numberProperty(service, CFSTR("idProduct"))),
           numberProperty(service, CFSTR("locationID")));
        IOObjectRelease(service);
    }
    IOObjectRelease(iter);
}

QVector<UVCControlMac::DeviceInfo> UVCControlMac::enumerate(quint16 vid, quint16 pid)
{
    QVector<DeviceInfo> devices;
    forEachUsbDevice([&](io_service_t service, quint16 dVid, quint16 dPid, quint32 loc) {
        if ((vid == 0 || dVid == vid) && (pid == 0 || dPid == pid)) {
            DeviceInfo info;
            info.vid = dVid;
            info.pid = dPid;
            info.locationID = loc;
            info.name = stringProperty(service, CFSTR("USB Product Name"));
            devices.append(info);
        }
    });
    return devices;
}

bool UVCControlMac::open(quint16 vid, quint16 pid, quint32 locationID)
{
    close();

    // 1) Find the USB device service.
    io_service_t deviceService = IO_OBJECT_NULL;
    forEachUsbDevice([&](io_service_t service, quint16 dVid, quint16 dPid, quint32 loc) {
        if (deviceService == IO_OBJECT_NULL && dVid == vid && dPid == pid
            && (locationID == 0 || loc == locationID)) {
            IOObjectRetain(service);
            deviceService = service;
            m_locationID = loc;
        }
    });
    if (deviceService == IO_OBJECT_NULL) {
        m_lastError = QStringLiteral("no USB device %1:%2 found")
                          .arg(vid, 4, 16, QLatin1Char('0'))
                          .arg(pid, 4, 16, QLatin1Char('0'));
        m_lastIOReturn = -1;
        return false;
    }

    // 2) Device plug-in -> IOUSBDeviceInterface. Used only to walk the
    //    interfaces; the device itself is never opened.
    IOCFPlugInInterface **plugin = nullptr;
    SInt32 score = 0;
    IOReturn kr = IOCreatePlugInInterfaceForService(deviceService, kIOUSBDeviceUserClientTypeID,
                                                    kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(deviceService);
    if (kr != kIOReturnSuccess || !plugin) {
        m_lastError = QStringLiteral("device plug-in: ") + ioReturnToString(kr);
        m_lastIOReturn = kr;
        return false;
    }
    IOUSBDeviceInterface **device = nullptr;
    (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
                              (LPVOID *)&device);
    IODestroyPlugInInterface(plugin);
    if (!device) {
        m_lastError = QStringLiteral("could not get IOUSBDeviceInterface");
        m_lastIOReturn = -1;
        return false;
    }

    // 3) Find the VideoControl interface. Iterating does not require the
    //    device to be open.
    IOUSBFindInterfaceRequest req;
    req.bInterfaceClass = kUSBVideoInterfaceClass;         // CC_VIDEO (14)
    req.bInterfaceSubClass = kUSBVideoControlSubClass;     // SC_VIDEOCONTROL (1)
    req.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
    req.bAlternateSetting = kIOUSBFindInterfaceDontCare;
    io_iterator_t ifIter = IO_OBJECT_NULL;
    kr = (*device)->CreateInterfaceIterator(device, &req, &ifIter);
    if (kr != kIOReturnSuccess) {
        m_lastError = QStringLiteral("interface iterator: ") + ioReturnToString(kr);
        m_lastIOReturn = kr;
        (*device)->Release(device);
        return false;
    }

    // Check !m_intf BEFORE pulling from the iterator: IOIteratorNext hands us
    // an owned reference, and fetching one after a match would leak it (the
    // release lives in the loop body).
    io_service_t ifService;
    while (!m_intf && (ifService = IOIteratorNext(ifIter)) != IO_OBJECT_NULL) {
        IOCFPlugInInterface **ifPlugin = nullptr;
        kr = IOCreatePlugInInterfaceForService(ifService, kIOUSBInterfaceUserClientTypeID,
                                               kIOCFPlugInInterfaceID, &ifPlugin, &score);
        IOObjectRelease(ifService);
        if (kr != kIOReturnSuccess || !ifPlugin)
            continue;
        IntfHandle intf = nullptr;
        (*ifPlugin)->QueryInterface(ifPlugin, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID),
                                    (LPVOID *)&intf);
        IODestroyPlugInInterface(ifPlugin);
        if (!intf)
            continue;

        UInt8 ifNumber = 0;
        (*intf)->GetInterfaceNumber(intf, &ifNumber);
        m_vcInterfaceNumber = ifNumber;
        // Deliberately NO USBInterfaceOpen() here - see the header comment.
        m_intf = intf;
    }
    IOObjectRelease(ifIter);
    (*device)->Release(device);

    if (!m_intf) {
        m_lastError = QStringLiteral("device has no VideoControl interface (not a UVC camera?)");
        m_lastIOReturn = -1;
        return false;
    }
    return true;
}

void UVCControlMac::close()
{
    if (m_intf) {
        IntfHandle intf = (IntfHandle)m_intf;
        // Never opened, so nothing to USBInterfaceClose(); just drop the ref.
        (*intf)->Release(intf);
        m_intf = nullptr;
    }
    m_vcInterfaceNumber = 0;
    m_locationID = 0;
}

UVCControlMac::~UVCControlMac()
{
    close();
}

bool UVCControlMac::controlRequest(const UVCRequest::SetupPacket &packet, void *data)
{
    if (!m_intf) {
        m_lastError = QStringLiteral("not open");
        m_lastIOReturn = -1;
        return false;
    }
    IntfHandle intf = (IntfHandle)m_intf;

    IOUSBDevRequest devReq;
    devReq.bmRequestType = packet.bmRequestType;
    devReq.bRequest = packet.bRequest;
    devReq.wValue = packet.wValue;
    devReq.wIndex = packet.wIndex;
    devReq.wLength = packet.wLength;
    devReq.pData = data;
    devReq.wLenDone = 0;

    // Pipe 0 = the default control pipe; the only pipe reachable without
    // opening the interface, and the only one we need.
    const IOReturn kr = (*intf)->ControlRequest(intf, 0, &devReq);
    m_lastIOReturn = kr;
    if (kr != kIOReturnSuccess) {
        m_lastError = ioReturnToString(kr);
        return false;
    }
    if (devReq.wLenDone != packet.wLength) {
        m_lastError = QStringLiteral("short transfer (%1 of %2 bytes)")
                          .arg(devReq.wLenDone).arg(packet.wLength);
        m_lastIOReturn = -1;
        return false;
    }
    return true;
}

bool UVCControlMac::setCur(quint8 unitId, quint8 selector, quint16 value)
{
    quint8 buf[2];
    UVCRequest::encodeLE16(value, buf);
    const bool ok = controlRequest(
        UVCRequest::makeSet(m_vcInterfaceNumber, unitId, selector, sizeof(buf)), buf);
    if (m_writeSettleUs > 0)
        QThread::usleep(ulong(m_writeSettleUs));
    return ok;
}

bool UVCControlMac::getCur(quint8 unitId, quint8 selector, quint16 *value,
                           UVCRequest::RequestCode code)
{
    quint8 buf[2] = {0, 0};
    if (!controlRequest(
            UVCRequest::makeGet(m_vcInterfaceNumber, unitId, selector, sizeof(buf), code), buf))
        return false;
    *value = UVCRequest::decodeLE16(buf);
    return true;
}
