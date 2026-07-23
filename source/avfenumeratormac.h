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

// Decision of WHICH USB device the Miniscope control channel opens for a user
// config's deviceID (an AVFoundation/OpenCV index). Split out as a pure
// function because getting it wrong is the worst multi-scope failure mode:
// frames from one scope with LED/gain commands silently driving another.
struct ControlTarget {
    bool ok = false;
    quint32 locationID = 0;  // USB locationID to open when ok
    QString warning;         // non-fatal, surface to the user when non-empty
    QString error;           // the reason, when !ok
};

// cameras/cameraID: the AVFoundation list and the config's index into it.
// expectedVid/Pid: the Miniscope DAQ USB identity (for a sanity warning).
// attachedLocations: locationIDs of every attached device with that identity
// (UVCControlMac::enumerate) - the fallback set when the index doesn't
// resolve. Falling back is only allowed when that set has exactly ONE entry;
// with several Miniscopes attached an unresolvable index is an error, never
// a guess.
ControlTarget resolveControlTarget(const QVector<AvfCameraInfo> &cameras, int cameraID,
                                   quint16 expectedVid, quint16 expectedPid,
                                   const QVector<quint32> &attachedLocations);

// Current AVF/OpenCV list index of the camera with this USB locationID, or -1.
// The camera list shifts whenever devices come and go (an iPhone joining via
// Continuity Camera is the classic case), so a config's stored deviceID can
// silently start pointing at the wrong camera; this re-binds the frame stream
// to the physical device the control channel already resolved.
inline int avfIndexForLocation(const QVector<AvfCameraInfo> &cameras, quint32 locationID)
{
    if (locationID == 0)
        return -1;
    for (int i = 0; i < cameras.size(); i++)
        if (cameras[i].isUsb && cameras[i].locationID == locationID)
            return i;
    return -1;
}

// The device's stable AVFoundation uniqueID (empty when absent) - what the
// frame grabber pins its capture session to, so the video stream can never
// land on a different camera than the control channel.
inline QString avfUniqueIdForLocation(const QVector<AvfCameraInfo> &cameras, quint32 locationID)
{
    const int i = avfIndexForLocation(cameras, locationID);
    return i < 0 ? QString() : cameras[i].uniqueID;
}

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
