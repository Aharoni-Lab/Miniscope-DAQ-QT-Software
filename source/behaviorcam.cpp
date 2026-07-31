#include "behaviorcam.h"
#include "newquickview.h"
#include "videodisplay.h"

#include <QQuickView>
#include <QQuickItem>
#include <QSemaphore>
#include <QObject>
#include <QTimer>
#include <QAtomicInt>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QQmlApplicationEngine>
#include <QVector>
#include <QVariant>

BehaviorCam::BehaviorCam(QObject *parent, QJsonObject ucDevice, qint64 softwareStartTime) :
    // A MiniCAM is Miniscope DAQ hardware - its MT9P031 sensor, FPD-Link SERDES
    // pair and LED driver are all brought up over the DAQ's I2C tunnel - so it
    // needs the direct-control backend exactly like a Miniscope does. Without
    // this it lands on VideoStreamOCV, whose macOS path has no control channel
    // and DISCARDS the whole command queue (VideoStreamOCV::sendCommands), so
    // the sensor is never configured and no frame ever arrives.
    //
    // Same webcam/MiniCAM test as isMiniCAM below, duplicated here rather than
    // shared because the base ctor picks the backend before this ctor's body
    // runs and so cannot read the member.
    VideoDevice(parent, ucDevice, softwareStartTime,
                /*preferDirectControl=*/!ucDevice.value("deviceType").toString()
                                             .toLower().contains("webcam")),
    m_softwareStartTime(softwareStartTime)
{
    m_ucDevice = ucDevice; // hold user config for this device
    m_cDevice = getDeviceConfig(m_ucDevice.value("deviceType").toString());

    // TODO: Handle cases where there is more than webcams and MiniCAMs
    if (m_ucDevice.value("deviceType").toString().toLower().contains("webcam")) {
        isMiniCAM = false;

        // USED BEFORE VIDEODEVICE CLASS
//        m_daqFrameNum = nullptr;
    }
    else {
        isMiniCAM = true;
    }

}

void BehaviorCam::setupDisplayObjectPointers()
{
    // display object can only be accessed after backend call createView()
    rootDistplayObject = getRootDisplayObject();
    deviceStream = getDeviceStream();

    // do stuff specific for behav cam window
    // Open OpenCV properties dialog for behav cam
    if (!isMiniCAM) {
        rootDistplayObject->findChild<QQuickItem*>("camProps")->setProperty("visible", true);
        QObject::connect(rootDistplayObject, SIGNAL( camPropsClicked() ), this, SLOT( handleCamPropsClicked()));
        QObject::connect(this, SIGNAL( openCamPropsDialog()), deviceStream, SLOT( openCamPropsDialog()));
    }
}

void BehaviorCam::handleNewDisplayFrame(qint64 /*timeStamp*/, cv::Mat frame, int /*bufIdx*/, VideoDisplay *vidDisp)
{
    QImage tempFrame2;
    cv::Mat tempFrame, tempMat1, tempMat2;
    // TODO: Think about where color to gray and vise versa should take place.
    if (frame.channels() == 1) {
        cv::cvtColor(frame, tempFrame, cv::COLOR_GRAY2BGR);
        tempFrame2 = QImage(tempFrame.data, tempFrame.cols, tempFrame.rows, tempFrame.step, QImage::Format_RGB888);
    }
    else
        tempFrame2 = QImage(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);

    vidDisp->setDisplayFrame(tempFrame2);

    if (isMiniCAM == false)
        vidDisp->setDroppedFrameCount(-1); // This overwrites display value in videodevice sendNewFrame function

}

