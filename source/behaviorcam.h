#ifndef BEHAVIORCAM_H
#define BEHAVIORCAM_H

#include <QObject>
#include <QThread>
#include <QSemaphore>
#include <QTimer>
#include <QAtomicInt>
#include <QJsonObject>
#include <QQuickView>
#include <QMap>
#include <QVector>
#include <QQuickItem>
#include <QVariant>

#include "videostreamocv.h"
#include "videodisplay.h"
#include "newquickview.h"
#include "videodevice.h"

#include <opencv2/opencv.hpp>

class BehaviorCam : public VideoDevice
{
    Q_OBJECT
public:
    explicit BehaviorCam(QObject *parent = nullptr, QJsonObject ucMiniscope = QJsonObject(), qint64 softwareStartTime = 0);
    void setupDisplayObjectPointers(); //overrides parents function
    void handleNewDisplayFrame(qint64 timeStamp, cv::Mat frame, int bufIdx, VideoDisplay* vidDisp); //overrides

signals:
    // THINK THIS IS UNUSED!!
    void newFrameAvailable(QString name, int frameNum);

    // THIS STAYS WITH BEHAV CLASS!!
    void openCamPropsDialog();

public slots:
    // LEAVE THIS!!!!
    void handleCamPropsClicked() { emit openCamPropsDialog();}

    // NOT SURE WHAT TO DO WITH THESE!!
    // Camera calibration slots
    void handleCamCalibClicked();
    void handleCamCalibStart();
    void handleCamCalibQuit();


private:
    QJsonObject m_ucDevice;
    QJsonObject m_cDevice;

    QObject* rootDistplayObject;
    VideoStreamOCV *deviceStream;
    // Handle MiniCAM stuff
    bool isMiniCAM;

    // Camera Calibration Vars
    bool m_camCalibWindowOpen;
    bool m_camCalibRunning;

    qint64 m_softwareStartTime;


};

#endif // BEHAVIORCAM_H
