#include "miniscope.h"
#include "newquickview.h"
#include "videodisplay.h"
#include "videodevice.h"

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
#include <QString>


Miniscope::Miniscope(QObject *parent, QJsonObject ucDevice, qint64 softwareStartTime) :
    VideoDevice(parent, ucDevice, softwareStartTime),
    baselineFrameBufWritePos(0),
    baselinePreviousTimeStamp(0),
    m_displatState("Raw"),
    m_softwareStartTime(softwareStartTime)
{
    // TODO: Maybe move to own function
    // For BNO display ----

    // Sets color of traces
    float c0[] = {1,0,0};
    float c1[] = {0,1,0};
    float c2[] = {0,0,1};
    for (int i=0; i < 3; i++) {
        bnoTraceColor[0][i] = c0[i];
        bnoTraceColor[1][i] = c1[i];
        bnoTraceColor[2][i] = c2[i];
    }

    for (int i=0; i < 3; i++) {
        bnoScale[i] = 1.0f;
        bnoDisplayBufNum[i] = 1;
        bnoNumDataInBuf[i][0] = 0;
        bnoNumDataInBuf[i][1] = 0;

    }
    // --------------------

    // For Neuron Trace Display
    m_numTraces = 0;
    // --------------------

    m_ucDevice = ucDevice; // hold user config for this device
    m_cDevice = getDeviceConfig(m_ucDevice["deviceType"].toString());

//    QObject::connect(this, &VideoDevice::displayCreated, this, &Miniscope::displayHasBeenCreated);
//    QObject::connect(this, &Miniscope::displayCreated, this, &Miniscope::displayHasBeenCreated);

}
void Miniscope::setupDisplayObjectPointers()
{
    // display object can only be accessed after backend call createView()
    rootDisplayObject = getRootDisplayObject();
    if (getHeadOrienataionStreamState())
        bnoDisplay = getRootDisplayChild("bno");
}
void Miniscope::handleDFFSwitchChange(bool checked)
{
    qDebug() << "Switch" << checked;
    if (checked)
        m_displatState = "dFF";
    else
        m_displatState = "Raw";
}

void Miniscope::handleAddNewTraceROI(int leftEdge, int topEdge, int width, int height)
{
    if (m_numTraces < NUM_MAX_NEURON_TRACES) {

        m_traceROIs[m_numTraces][0] = leftEdge;
        m_traceROIs[m_numTraces][1] = topEdge;
        m_traceROIs[m_numTraces][2] = width;
        m_traceROIs[m_numTraces][3] = height;


        m_traceColors[m_numTraces][0] = 1.0f;
        m_traceColors[m_numTraces][1] = 1.0f;
        m_traceColors[m_numTraces][2] = 0.3f;

        m_traceDisplayBufNum[m_numTraces] = 1;
        m_traceNumDataInBuf[m_numTraces][0] = 0;
        m_traceNumDataInBuf[m_numTraces][1] = 0;

        emit addTraceDisplay("Neuron" + QString::number(m_numTraces),
                             m_traceColors[m_numTraces],
                             1.0/255.0f,
                             &m_traceDisplayBufNum[m_numTraces],
                             m_traceNumDataInBuf[m_numTraces],
                             TRACE_DISPLAY_BUFFER_SIZE,
                             m_traceDisplayT[m_numTraces][0],
                             m_traceDisplayY[m_numTraces][0]);

        m_numTraces++;
    }
}


void Miniscope::handleNewDisplayFrame(qint64 timeStamp, cv::Mat frame, int bufIdx, VideoDisplay *vidDisp)
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

    // Generate moving average baseline frame
    if ((timeStamp - baselinePreviousTimeStamp) > 100) {
        // update baseline frame buffer every ~500ms
        tempMat1 = frame.clone();
        tempMat1.convertTo(tempMat1, CV_32F);
        tempMat1 = tempMat1/(BASELINE_FRAME_BUFFER_SIZE);
        if (baselineFrameBufWritePos == 0) {
            baselineFrame = tempMat1;
        }
        else if (baselineFrameBufWritePos < BASELINE_FRAME_BUFFER_SIZE) {
            baselineFrame += tempMat1;
        }
        else {
            baselineFrame += tempMat1;
            baselineFrame -= baselineFrameBuffer[baselineFrameBufWritePos%BASELINE_FRAME_BUFFER_SIZE];
        }
        baselineFrameBuffer[baselineFrameBufWritePos % BASELINE_FRAME_BUFFER_SIZE] = tempMat1.clone();
        baselinePreviousTimeStamp = timeStamp;
        baselineFrameBufWritePos++;
    }

    if (m_displatState == "Raw") {

//            vidDisplay->setDisplayFrame(tempFrame2.copy());
        // TODO: Check to see if we can get rid of .copy() here
        vidDisp->setDisplayFrame(tempFrame2);
    }
    else if (m_displatState == "dFF") {
        // TODO: Implement this better. I am sure it can be sped up a lot. Maybe do most of it in a shader
        tempMat2 = frame.clone();
        tempMat2.convertTo(tempMat2, CV_32F);
        cv::divide(tempMat2,baselineFrame,tempMat2);
        tempMat2 = ((tempMat2 - 1.0) + 0.5) * 255;
        tempMat2.convertTo(tempMat2, CV_8U);
        cv::cvtColor(tempMat2, tempFrame, cv::COLOR_GRAY2BGR);
        tempFrame2 = QImage(tempFrame.data, tempFrame.cols, tempFrame.rows, tempFrame.step, QImage::Format_RGB888);
        vidDisp->setDisplayFrame(tempFrame2.copy()); //TODO: Probably doesn't need "copy"
    }
    if (getHeadOrienataionStreamState()) {
        if (bnoBuffer[bufIdx*5+4] < 0.05) { // Checks to see if norm of quat differs from 1 by 0.05
            // good data
            bnoDisplay->setProperty("badData", false);
            bnoDisplay->setProperty("qw", bnoBuffer[bufIdx*5+0]);
            bnoDisplay->setProperty("qx", bnoBuffer[bufIdx*5+1]);
            bnoDisplay->setProperty("qy", bnoBuffer[bufIdx*5+2]);
            bnoDisplay->setProperty("qz", bnoBuffer[bufIdx*5+3]);

            // fill BNO display buffers ---------------
            int bufNum;
            int dataCount;
            for (int bnoIdx=0; bnoIdx < 3; bnoIdx++) {

                if (bnoDisplayBufNum[bnoIdx] == 0)
                    bufNum = 1;
                else
                    bufNum = 0;
                dataCount  = bnoNumDataInBuf[bnoIdx][bufNum];
                if (dataCount < TRACE_DISPLAY_BUFFER_SIZE) {
                    // There is space for more data
                    bnoTraceDisplayY[bnoIdx][bufNum][dataCount] = bnoBuffer[bufIdx*5 + 1 + bnoIdx];
                    bnoTraceDisplayT[bnoIdx][bufNum][dataCount] = (timeStamp - m_softwareStartTime)/1000.0;
                    bnoNumDataInBuf[bnoIdx][bufNum]++;
                }
            }
            // -----------------------------------------
        }
        else {
            // bad BNO data
            bnoDisplay->setProperty("badData", true);
        }
    }
    if (m_numTraces > 0) {
        int bufNum;
        int dataCount;
        float meanIntensity;

        for (int i=0; i < m_numTraces; i++) {
//            m_traceROIs[m_numTraces][0] = leftEdge;
//            m_traceROIs[m_numTraces][1] = topEdge;
//            m_traceROIs[m_numTraces][2] = width;
//            m_traceROIs[m_numTraces][3] = height;
            cv::Rect roiRect(m_traceROIs[i][0],
                            m_traceROIs[i][1],
                            m_traceROIs[i][2],
                            m_traceROIs[i][3]);

            if (m_traceDisplayBufNum[i] == 0)
                bufNum = 1;
            else
                bufNum = 0;
            dataCount  = m_traceNumDataInBuf[i][bufNum];
            if (dataCount < TRACE_DISPLAY_BUFFER_SIZE) {
                // There is space for more data
                meanIntensity = cv::mean(frame(roiRect))[0];

                m_traceDisplayY[i][bufNum][dataCount] = meanIntensity;
                m_traceDisplayT[i][bufNum][dataCount] = (timeStamp - m_softwareStartTime)/1000.0;
                m_traceNumDataInBuf[i][bufNum]++;
            }
        }
    }
}

void Miniscope::setupBNOTraceDisplay()
{
    // Setup 3 traces for BNO data
    QString name;
    for (int i=0; i < 3; i++) {
        if (i ==0)
            name = "Roll";
        else if (i == 1)
            name = "Pitch";
        else if (i == 2)
            name = "Yaw";
        emit addTraceDisplay(name,
                             bnoTraceColor[i],
                             bnoScale[i],
                             &bnoDisplayBufNum[i],
                             bnoNumDataInBuf[i],
                             TRACE_DISPLAY_BUFFER_SIZE,
                             bnoTraceDisplayT[i][0],
                             bnoTraceDisplayY[i][0]);
    }
}

