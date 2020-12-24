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

#define TRACE_DISPLAY_BUFFER_SIZE         256
#define NUM_MAX_NEURON_TRACES             32
#define SMOOTHING_WINDOW_IN_FRAMES        4

class Miniscope : public VideoDevice
{
    Q_OBJECT
public:
    explicit Miniscope(QObject *parent = nullptr, QJsonObject ucMiniscope = QJsonObject(), qint64 softwareStartTime = 0);
    void setupDisplayObjectPointers() override; //overrides parents function
    float* getBNOBufferPointer() { return bnoBuffer; }
//    void sendNewFrame(); // overrides parent function
    void handleNewDisplayFrame(qint64 timeStamp, cv::Mat frame, int bufIdx, VideoDisplay* vidDisp) override; //overrides


    void setupBNOTraceDisplay() override; // overrides parent
public slots:
//    void displayHasBeenCreated();
    void handleDFFSwitchChange(bool checked);
    void handleAddNewTraceROI(int leftEdge, int topEdge, int width, int height) override;

private:
    QJsonObject m_ucDevice;
    QJsonObject m_cDevice;

    QObject* rootDisplayObject;
    VideoDisplay* vidDisplay;

//    float bnoBuffer[FRAME_BUFFER_SIZE*5]; //w,x,y,z,norm
    QQuickItem *bnoDisplay;


    cv::Mat baselineFrameBuffer[BASELINE_FRAME_BUFFER_SIZE];
    cv::Mat baselineFrame;
    int baselineFrameBufWritePos;
    qint64 baselinePreviousTimeStamp;

    QString m_displatState; // holds raw of dff view state

    // For BNO trace display
    float bnoTraceColor[3][3];
    float bnoScale[3];
    QAtomicInt bnoDisplayBufNum[3];
    QAtomicInt bnoNumDataInBuf[3][2];
    float bnoTraceDisplayY[3][2][TRACE_DISPLAY_BUFFER_SIZE];
    float bnoTraceDisplayT[3][2][TRACE_DISPLAY_BUFFER_SIZE];

    // For Neuron Trace Display
    int m_numTraces;
    int m_traceROIs[NUM_MAX_NEURON_TRACES][4];
    float m_traceColors[NUM_MAX_NEURON_TRACES][3];
    QAtomicInt m_traceDisplayBufNum[NUM_MAX_NEURON_TRACES];
    QAtomicInt m_traceNumDataInBuf[NUM_MAX_NEURON_TRACES][2];
    float m_traceDisplayY[NUM_MAX_NEURON_TRACES][2][TRACE_DISPLAY_BUFFER_SIZE];
    float m_traceDisplayT[NUM_MAX_NEURON_TRACES][2][TRACE_DISPLAY_BUFFER_SIZE];
    float m_traceLastValues[NUM_MAX_NEURON_TRACES][SMOOTHING_WINDOW_IN_FRAMES];
    int m_traceLastValueIdx;


    qint64 m_softwareStartTime;

};


#endif // MINISCOPE_H
