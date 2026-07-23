#ifndef AVFENUMERATORMAC_H
#define AVFENUMERATORMAC_H

#include <QString>
#include <QStringList>
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

// Behavior cameras (webcams) are configured by deviceID = AVFoundation list
// index. Resolve the index to the camera's stable uniqueID ONCE at connect;
// after that the capture session is pinned to the uniqueID and reconnects
// re-resolve by uniqueID only, never by index - list positions shift whenever
// cameras come and go, and an index reopen can silently bind a DIFFERENT
// physical camera (the "ghost reconnect" observed on the bench).
struct WebcamTarget {
    bool ok = false;
    QString uniqueID;
    QString name;      // localizedName, for user-facing messages
    QString error;     // the reason, when !ok
};

inline WebcamTarget resolveWebcamTarget(const QVector<AvfCameraInfo> &cameras, int cameraID)
{
    WebcamTarget target;
    if (cameras.isEmpty()) {
        target.error = QStringLiteral("no cameras are visible to macOS.");
        return target;
    }
    if (cameraID < 0 || cameraID >= cameras.size()) {
        QStringList names;
        for (int i = 0; i < cameras.size(); i++)
            names.append(QStringLiteral("deviceID %1: %2").arg(i).arg(cameras[i].name));
        target.error = QStringLiteral("deviceID %1 is out of range - %2 camera(s) connected (%3).")
                           .arg(cameraID).arg(cameras.size()).arg(names.join(QStringLiteral(", ")));
        return target;
    }
    target.ok = true;
    target.uniqueID = cameras[cameraID].uniqueID;
    target.name = cameras[cameraID].name;
    return target;
}

// The stable AVFoundation uniqueID of the camera with this USB locationID
// (empty when absent) - what the frame grabber pins its capture session to,
// so the video stream can never land on a different camera than the control
// channel. Deliberately NOT an index lookup: list positions shift whenever
// cameras come and go (iPhone Continuity Camera, hot-plugs).
inline QString avfUniqueIdForLocation(const QVector<AvfCameraInfo> &cameras, quint32 locationID)
{
    if (locationID == 0)
        return QString();
    for (const AvfCameraInfo &cam : cameras)
        if (cam.isUsb && cam.locationID == locationID)
            return cam.uniqueID;
    return QString();
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
