#ifndef AVFENUMERATORMAC_H
#define AVFENUMERATORMAC_H

#include <QString>
#include <QVector>
#include <QtGlobal>

// Cameras as AVFoundation sees them, in the SAME order OpenCV's
// CAP_AVFOUNDATION backend indexes them - so the position in this list IS the
// user config's deviceID. (OpenCV 4.x builds its device list as
// devicesWithMediaType:Video + devicesWithMediaType:Muxed; the .mm mirrors
// that call exactly, deprecation warning and all.)
struct AvfCameraInfo {
    QString name;            // localizedName, e.g. "FaceTime HD Camera"
    QString uniqueID;        // AVCaptureDevice.uniqueID
    quint32 locationID = 0;  // parsed from uniqueID for USB cameras; 0 otherwise
    quint16 vid = 0;
    quint16 pid = 0;
    bool isUsb = false;      // true when uniqueID parsed as a USB device
};

QVector<AvfCameraInfo> enumerateAvfCameras();

// AVCaptureDevice.uniqueID for a USB camera is the hex of the 64-bit value
// (locationID << 32) | (vid << 16) | pid, with or without a "0x" prefix and
// possibly without leading zeros. Non-USB devices (the built-in camera on
// Apple Silicon, Continuity Camera) use opaque UUIDs, which fail the hex
// parse - returns false for those. This is the bridge from an AVFoundation /
// OpenCV device index to the USB device UVCControlMac must open.
inline bool parseAvfUsbUniqueId(const QString &uniqueID, quint32 *locationID,
                                quint16 *vid, quint16 *pid)
{
    QString hex = uniqueID.startsWith(QStringLiteral("0x")) ? uniqueID.mid(2) : uniqueID;
    if (hex.isEmpty() || hex.size() > 16)
        return false;
    bool ok = false;
    const quint64 v = hex.toULongLong(&ok, 16);
    if (!ok)
        return false;
    *locationID = quint32(v >> 32);
    *vid = quint16((v >> 16) & 0xFFFF);
    *pid = quint16(v & 0xFFFF);
    // A USB camera always has a non-zero location and vendor ID; a short
    // opaque ID that happened to parse as hex won't.
    return *locationID != 0 && *vid != 0;
}

#endif // AVFENUMERATORMAC_H
