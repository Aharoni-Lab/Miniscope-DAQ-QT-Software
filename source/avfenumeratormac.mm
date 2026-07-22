#include "avfenumeratormac.h"

#import <AVFoundation/AVFoundation.h>

// Deliberately the same deprecated API OpenCV's cap_avfoundation_mac.mm calls
// (devicesWithMediaType:Video then :Muxed): matching the call is what makes
// our list order equal CAP_AVFOUNDATION's deviceID order. Do not "modernize"
// to AVCaptureDeviceDiscoverySession unless OpenCV does the same.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

QVector<AvfCameraInfo> enumerateAvfCameras()
{
    QVector<AvfCameraInfo> cameras;
    @autoreleasepool {
        NSArray *devices = [[AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo]
            arrayByAddingObjectsFromArray:[AVCaptureDevice devicesWithMediaType:AVMediaTypeMuxed]];
        for (AVCaptureDevice *device in devices) {
            AvfCameraInfo info;
            info.name = QString::fromNSString(device.localizedName);
            info.uniqueID = QString::fromNSString(device.uniqueID);
            info.isUsb = parseAvfUsbUniqueId(info.uniqueID, &info.locationID,
                                             &info.vid, &info.pid);
            cameras.append(info);
        }
    }
    return cameras;
}

#pragma clang diagnostic pop

ControlTarget resolveControlTarget(const QVector<AvfCameraInfo> &cameras, int cameraID,
                                   quint16 expectedVid, quint16 expectedPid,
                                   const QVector<quint32> &attachedLocations)
{
    ControlTarget target;

    if (cameraID >= 0 && cameraID < cameras.size() && cameras[cameraID].isUsb) {
        const AvfCameraInfo &cam = cameras[cameraID];
        if (cam.vid != expectedVid || cam.pid != expectedPid)
            target.warning = QStringLiteral("deviceID %1 (%2) does not look like a Miniscope DAQ.")
                                 .arg(cameraID).arg(cam.name);
        target.ok = true;
        target.locationID = cam.locationID;
        return target;
    }

    // The index doesn't resolve to a USB camera (out of range, a non-USB
    // camera, or an opaque uniqueID). "Whichever Miniscope IOKit finds" is
    // only the right answer when there is exactly one to find.
    if (attachedLocations.size() == 1) {
        target.ok = true;
        target.locationID = attachedLocations.first();
        target.warning = QStringLiteral("deviceID %1 could not be matched to a USB camera; "
                                        "using the only Miniscope DAQ attached.")
                             .arg(cameraID);
        return target;
    }
    if (attachedLocations.isEmpty())
        target.error = QStringLiteral("deviceID %1 could not be matched to a USB camera "
                                      "and no Miniscope DAQ was found on the USB bus.")
                           .arg(cameraID);
    else
        target.error = QStringLiteral("deviceID %1 could not be matched to a USB camera and "
                                      "%2 Miniscope DAQs are attached - refusing to guess "
                                      "which one is meant. Unplug the extras or fix the "
                                      "deviceID in the user config.")
                           .arg(cameraID).arg(attachedLocations.size());
    return target;
}
