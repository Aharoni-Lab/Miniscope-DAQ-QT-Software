#include "uvccontrolmac.h"
#include "uvcrequest.h"

#include <QDebug>
#include <QSet>
#include <QThread>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USBSpec.h>

// USB class/subclass of the UVC VideoControl interface (UVC 1.1 spec):
// CC_VIDEO / SC_VIDEOCONTROL.
static const quint8 kUsbVideoClass = 14;
static const quint8 kUsbVideoControlSubclass = 1;

// Settle time after each transfer; the Miniscope DAQ's control endpoint is
// slow to clear (same constraint as the other backends' inter-command waits).
static const int kCtrlSettleUs = 200;

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

// Read a numeric registry property (idVendor / idProduct / locationID).
static quint32 numberProperty(io_service_t service, CFStringRef key, bool *ok = nullptr)
{
    quint32 out = 0;
    bool valid = false;
    CFTypeRef ref = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    if (ref) {
        if (CFGetTypeID(ref) == CFNumberGetTypeID())
            valid = CFNumberGetValue((CFNumberRef)ref, kCFNumberSInt32Type, &out);
        CFRelease(ref);
    }
    if (ok)
        *ok = valid;
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
// locationID); fn returns true to stop early. Matches both the legacy
// kIOUSBDeviceClassName and the modern "IOUSBHostDevice" registration (the
// legacy name is a compatibility alias on current macOS, but matching both and
// deduplicating by locationID costs little and survives either stack).
template <typename Fn>
static void forEachUsbDevice(Fn fn)
{
    static const char *const kDeviceClasses[] = { kIOUSBDeviceClassName, "IOUSBHostDevice" };
    QSet<quint64> seen;   // (locationID << 32) | vid << 16 | pid
    bool stopped = false;
    for (const char *deviceClass : kDeviceClasses) {
        if (stopped)
            break;
        CFMutableDictionaryRef match = IOServiceMatching(deviceClass);
        if (!match)
            continue;
        io_iterator_t iter = IO_OBJECT_NULL;
        // Consumes 'match'.
        if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter) != KERN_SUCCESS)
            continue;
        io_service_t service;
        while ((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
            const quint16 vid = quint16(numberProperty(service, CFSTR("idVendor")));
            const quint16 pid = quint16(numberProperty(service, CFSTR("idProduct")));
            const quint32 loc = numberProperty(service, CFSTR("locationID"));
            const quint64 key = (quint64(loc) << 32) | (quint64(vid) << 16) | pid;
            bool stop = false;
            if (!seen.contains(key)) {
                seen.insert(key);
                stop = fn(service, vid, pid, loc);
            }
            IOObjectRelease(service);
            if (stop) {
                stopped = true;
                break;
            }
        }
        IOObjectRelease(iter);
    }
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
        return false; // keep iterating
    });
    return devices;
}

bool UVCControlMac::open(quint16 vid, quint16 pid, quint32 locationID)
{
    close();

    // 1) Find the USB device service.
    io_service_t deviceService = IO_OBJECT_NULL;
    forEachUsbDevice([&](io_service_t service, quint16 dVid, quint16 dPid, quint32 loc) {
        if (dVid == vid && dPid == pid && (locationID == 0 || loc == locationID)) {
            IOObjectRetain(service);
            deviceService = service;
            m_locationID = loc;
            return true;
        }
        return false;
    });
    if (deviceService == IO_OBJECT_NULL) {
        m_lastError = QStringLiteral("no USB device %1:%2 found")
                          .arg(vid, 4, 16, QLatin1Char('0'))
                          .arg(pid, 4, 16, QLatin1Char('0'));
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
        return false;
    }
    IOUSBDeviceInterface **device = nullptr;
    (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
                              (LPVOID *)&device);
    IODestroyPlugInInterface(plugin);
    if (!device) {
        m_lastError = QStringLiteral("could not get IOUSBDeviceInterface");
        return false;
    }

    // 3) Find the VideoControl interface (class 14, subclass 1). Iterating
    //    does not require the device to be open.
    IOUSBFindInterfaceRequest req;
    req.bInterfaceClass = kUsbVideoClass;
    req.bInterfaceSubClass = kUsbVideoControlSubclass;
    req.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
    req.bAlternateSetting = kIOUSBFindInterfaceDontCare;
    io_iterator_t ifIter = IO_OBJECT_NULL;
    kr = (*device)->CreateInterfaceIterator(device, &req, &ifIter);
    if (kr != kIOReturnSuccess) {
        m_lastError = QStringLiteral("interface iterator: ") + ioReturnToString(kr);
        (*device)->Release(device);
        return false;
    }

    io_service_t ifService;
    while ((ifService = IOIteratorNext(ifIter)) != IO_OBJECT_NULL && !m_intf) {
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

bool UVCControlMac::controlRequest(quint8 bmRequestType, quint8 bRequest, quint16 wValue,
                                   quint16 wIndex, quint16 wLength, void *data)
{
    if (!m_intf) {
        m_lastError = QStringLiteral("not open");
        return false;
    }
    IntfHandle intf = (IntfHandle)m_intf;

    IOUSBDevRequest devReq;
    devReq.bmRequestType = bmRequestType;
    devReq.bRequest = bRequest;
    devReq.wValue = wValue;
    devReq.wIndex = wIndex;
    devReq.wLength = wLength;
    devReq.pData = data;
    devReq.wLenDone = 0;

    // Pipe 0 = the default control pipe; the only pipe reachable without
    // opening the interface, and the only one we need.
    const IOReturn kr = (*intf)->ControlRequest(intf, 0, &devReq);
    QThread::usleep(kCtrlSettleUs);
    if (kr != kIOReturnSuccess) {
        m_lastError = ioReturnToString(kr);
        return false;
    }
    if (devReq.wLenDone != wLength) {
        m_lastError = QStringLiteral("short transfer (%1 of %2 bytes)")
                          .arg(devReq.wLenDone).arg(wLength);
        return false;
    }
    return true;
}

bool UVCControlMac::setCur(quint8 unitId, quint8 selector, quint16 value)
{
    quint8 buf[2];
    UVCRequest::encodeLE16(value, buf);
    const auto sp = UVCRequest::makeSet(m_vcInterfaceNumber, unitId, selector, sizeof(buf));
    return controlRequest(sp.bmRequestType, sp.bRequest, sp.wValue, sp.wIndex, sp.wLength, buf);
}

bool UVCControlMac::getCur(quint8 unitId, quint8 selector, quint16 *value)
{
    quint8 buf[2] = {0, 0};
    const auto sp = UVCRequest::makeGet(m_vcInterfaceNumber, unitId, selector, sizeof(buf));
    if (!controlRequest(sp.bmRequestType, sp.bRequest, sp.wValue, sp.wIndex, sp.wLength, buf))
        return false;
    *value = UVCRequest::decodeLE16(buf);
    return true;
}
