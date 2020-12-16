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
    VideoDevice(parent, ucDevice, softwareStartTime),
    m_camCalibWindowOpen(false),
    m_camCalibRunning(false),
    m_softwareStartTime(softwareStartTime)
{
    m_ucDevice = ucDevice; // hold user config for this device
    m_cDevice = getDeviceConfig(m_ucDevice["deviceType"].toString());

    // TODO: Handle cases where there is more than webcams and MiniCAMs
    if (m_ucDevice["deviceType"].toString().toLower().contains("webcam")) {
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

    // Handle camera calibration signals from GUI
    QObject::connect(rootDistplayObject, SIGNAL( calibrateCameraClicked() ), this, SLOT( handleCamCalibClicked()));
    QObject::connect(rootDistplayObject, SIGNAL( calibrateCameraStart() ), this, SLOT( handleCamCalibStart()));
    QObject::connect(rootDistplayObject, SIGNAL( calibrateCameraQuit() ), this, SLOT( handleCamCalibQuit()));

}

void BehaviorCam::handleNewDisplayFrame(qint64 timeStamp, cv::Mat frame, int bufIdx, VideoDisplay *vidDisp)
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

void BehaviorCam::handleCamCalibClicked()
{
    // This slot gets called when user clicks "camera calibration" in behavior cam GUI
    qDebug() << "Entering camera calibration";
    m_camCalibWindowOpen = true;
    // camCalibWindow will open up. This is located in the behaviorCam.qml file
    // This window will display directions begin/quit buttons, and progress of calibration

}

void BehaviorCam::handleCamCalibStart()
{
    qDebug() << "Beginning camera calibration";
    m_camCalibRunning = true;
    // Probably can use an if statement in sendNewFrame() to send frames somewhere for camera calibration
    // Probably want to update the cam calib window that opens up with info as the cam is being calibrated

    // When done, calibration should be saved in a file and the file path should be updated in the user config or
    // Another option would be to just save all the calibration data directly into the user config file

}

void BehaviorCam::handleCamCalibQuit()
{
    qDebug() << "Quitting camera calibration";
    if (m_camCalibRunning) {
        // Do stuff to exit cam calibration algorithm without issue
        m_camCalibRunning = false;
    }
    m_camCalibWindowOpen = false;
}

