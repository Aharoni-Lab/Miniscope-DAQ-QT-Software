#ifndef MINISCOPE_H
#define MINISCOPE_H

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

// ----- Used for dF/F display ---------
#define BASELINE_FRAME_BUFFER_SIZE  128


class Miniscope : public VideoDevice
{
    Q_OBJECT
public:
    explicit Miniscope(QObject *parent = nullptr, QJsonObject ucMiniscope = QJsonObject());
    void setupDisplayObjectPointers(); //overrides parents function
    float* getBNOBufferPointer() { return bnoBuffer; }
//    void sendNewFrame(); // overrides parent function
    void handleNewDisplayFrame(qint64 timeStamp, cv::Mat frame, int bufIdx, VideoDisplay* vidDisp); //overrides

public slots:
//    void displayHasBeenCreated();
    void handleDFFSwitchChange(bool checked);

private:
    QJsonObject m_ucDevice;
    QJsonObject m_cDevice;

    QObject* rootDistplayObject;

    float bnoBuffer[FRAME_BUFFER_SIZE*5]; //w,x,y,z,norm
    QQuickItem *bnoDisplay;


    cv::Mat baselineFrameBuffer[BASELINE_FRAME_BUFFER_SIZE];
    cv::Mat baselineFrame;
    int baselineFrameBufWritePos;
    qint64 baselinePreviousTimeStamp;

    QString m_displatState; // holds raw of dff view state
};


#endif // MINISCOPE_H
