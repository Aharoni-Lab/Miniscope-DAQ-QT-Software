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
