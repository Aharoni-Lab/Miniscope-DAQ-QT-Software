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
#include <QtMath>


Miniscope::Miniscope(QObject *parent, QJsonObject ucDevice, qint64 softwareStartTime) :
    VideoDevice(parent, ucDevice, softwareStartTime),
    baselineFrameBufWritePos(0),
    baselinePreviousTimeStamp(0),
    m_displatState("Raw"),
    m_softwareStartTime(softwareStartTime)
{

    // --------------------

    // For Neuron Trace Display
    m_numTraces = 0;
    m_traceLastValueIdx = 0;
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
    QObject* temp = getRootDisplayChild("addTraceRoi");
    temp->setProperty("enabled", getTraceDisplayStatus());
    vidDisplay = getVideoDisplay();
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
    double windowScale = m_ucDevice["windowScale"].toDouble(1);
    if (m_numTraces < NUM_MAX_NEURON_TRACES) {

//        qDebug() << leftEdge << topEdge << width << height;
        m_traceROIs[m_numTraces][0] = leftEdge/windowScale;
        m_traceROIs[m_numTraces][1] = topEdge/windowScale;
        m_traceROIs[m_numTraces][2] = width/windowScale;
        m_traceROIs[m_numTraces][3] = height/windowScale;


        m_traceColors[m_numTraces][0] = ((float)m_numTraces * 1.2 + 1)/4.0f;
        m_traceColors[m_numTraces][0] -= floor(m_traceColors[m_numTraces][0]);
        m_traceColors[m_numTraces][1] = -2.0f;
        m_traceColors[m_numTraces][2] = -2.0f;

        m_traceDisplayBufNum[m_numTraces] = 1;
        m_traceNumDataInBuf[m_numTraces][0] = 0;
        m_traceNumDataInBuf[m_numTraces][1] = 0;

        emit addTraceDisplay("Neuron" + QString::number(m_numTraces),
                             m_traceColors[m_numTraces],
                             1.0/255.0f,
                             "px",
                             false,
                             &m_traceDisplayBufNum[m_numTraces],
                             m_traceNumDataInBuf[m_numTraces],
                             TRACE_DISPLAY_BUFFER_SIZE,
                             m_traceDisplayT[m_numTraces][0],
                             m_traceDisplayY[m_numTraces][0]);

        // TODO: Must be a better way to append QVariant in qml
        QList<double> tempProp;
        QVariant varParams;

        tempProp = qvariant_cast< QList<double> >(vidDisplay->property("traceROIx"));
        tempProp.append(leftEdge);
        varParams.setValue<QList<double>>( tempProp );
        vidDisplay->setProperty("traceROIx", varParams);

        tempProp = qvariant_cast< QList<double> >(vidDisplay->property("traceROIy"));
        tempProp.append(topEdge);
        varParams.setValue<QList<double>>( tempProp );
        vidDisplay->setProperty("traceROIy", varParams);

        tempProp = qvariant_cast< QList<double> >(vidDisplay->property("traceROIw"));
        tempProp.append(width);
        varParams.setValue<QList<double>>( tempProp );
        vidDisplay->setProperty("traceROIw", varParams);

        tempProp = qvariant_cast< QList<double> >(vidDisplay->property("traceROIh"));
        tempProp.append(height);
        varParams.setValue<QList<double>>( tempProp );
        vidDisplay->setProperty("traceROIh", varParams);

        tempProp = qvariant_cast< QList<double> >(vidDisplay->property("traceColor"));
        tempProp.append(m_traceColors[m_numTraces][0]);
        varParams.setValue<QList<double>>( tempProp );
        vidDisplay->setProperty("traceColor", varParams);


        m_numTraces++;

    }
}


void Miniscope::handleNewDisplayFrame(qint64 timeStamp, cv::Mat frame, int bufIdx, VideoDisplay *vidDisp)
{
    QImage tempFrame2;
    cv::Mat tempFrame, tempMat1, tempMat2;
    float traceMean = 0;

//    for (int i=0; i < m_numTraces; i++) {
//        frame.at<uint8_t>(m_traceROIs[i][1],m_traceROIs[i][0]) = 255;
//        frame.at<uint8_t>(m_traceROIs[i][1] + m_traceROIs[i][3],m_traceROIs[i][0] + m_traceROIs[i][2]) = 255;
//    }

    // TODO: Think about where color to gray and vise versa should take place.
    if (frame.channels() == 1) {
        cv::cvtColor(frame, tempFrame, cv::COLOR_GRAY2BGR);
        tempFrame2 = QImage(tempFrame.data, tempFrame.cols, tempFrame.rows, tempFrame.step, QImage::Format_RGB888);
    }
    else
        tempFrame2 = QImage(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);

    // Generate moving average baseline frame
    if ((timeStamp - baselinePreviousTimeStamp) > 50) {
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
        tempMat2 = ((tempMat2 - 1.0) + 0.02) * 256 * 10;
        tempMat2.convertTo(tempMat2, CV_8U);
        cv::cvtColor(tempMat2, tempFrame, cv::COLOR_GRAY2BGR);
        tempFrame2 = QImage(tempFrame.data, tempFrame.cols, tempFrame.rows, tempFrame.step, QImage::Format_RGB888);
        vidDisp->setDisplayFrame(tempFrame2); //TODO: Probably doesn't need "copy"
    }
    if (getHeadOrienataionStreamState()) {
        // TODO: Clean up this section. Consolidate
        if (bnoBuffer[bufIdx*5+4] < 0.05) { // Checks to see if norm of quat differs from 1 by 0.05
            // good data
            bnoDisplay->setProperty("badData", false);
            bnoDisplay->setProperty("qw", bnoBuffer[bufIdx*5+0]);
            bnoDisplay->setProperty("qx", bnoBuffer[bufIdx*5+1]);
            bnoDisplay->setProperty("qy", bnoBuffer[bufIdx*5+2]);
            bnoDisplay->setProperty("qz", bnoBuffer[bufIdx*5+3]);

            // Figure out Euler Angles
            double qw = bnoBuffer[bufIdx*5+0];
            double qx = bnoBuffer[bufIdx*5+1];
            double qy = bnoBuffer[bufIdx*5+2];
            double qz = bnoBuffer[bufIdx*5+3];

            double m00 = 1.0 - 2.0*qy*qy - 2.0*qz*qz;
            double m01 = 2.0*qx*qy + 2.0*qz*qw;
            double m02 = 2.0*qx*qz - 2.0*qy*qw;
            double m10 = 2.0*qx*qy - 2.0*qz*qw;
            double m11 = 1 - 2.0*qx*qx - 2.0*qz*qz;
            double m12 = 2.0*qy*qz + 2.0*qx*qw;
            double m20 = 2.0*qx*qz + 2.0*qy*qw;
            double m21 = 2.0*qy*qz - 2.0*qx*qw;
            double m22 = 1.0 - 2.0*qx*qx - 2.0*qy*qy;


            double R = atan2(m12, m22);
            double c2 = sqrt(m00*m00 + m01*m01);
            double P = atan2(-m02, c2);
            double s1 = sin(R);
            double c1 = cos(R);
            double Y = atan2(s1*m20 - c1*m10, c1*m11-s1*m21);

            double euler[] = {R,P,Y};
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
                    bnoTraceDisplayY[bnoIdx][bufNum][dataCount] = euler[bnoIdx];
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

        float meanFrameIntensity;
        if (m_displatState == "dFF") {
            // Remove or find a fast way (maybe with resize or downsamp) if needed.
            // Use center 60% of frame for mean calculation
            cv::Rect frameMeanROIRect(tempMat2.rows * 0.2,
                                      tempMat2.cols * 0.2,
                                      tempMat2.rows * 0.6,
                                      tempMat2.cols * 0.6);
            meanFrameIntensity = cv::mean(tempMat2(frameMeanROIRect))[0];
        }
        else
            meanFrameIntensity = 0.0f;
        m_traceLastValueIdx = (m_traceLastValueIdx + 1) % SMOOTHING_WINDOW_IN_FRAMES;
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
                if (m_displatState == "Raw") {
                    meanIntensity = cv::mean(frame(roiRect))[0];
                }
                else if (m_displatState == "dFF") {
                    meanIntensity = cv::mean(tempMat2(roiRect))[0];
                }
                // Used for mean window smoothing of traces
                m_traceLastValues[i][m_traceLastValueIdx] = meanIntensity - meanFrameIntensity - 127.0f;
                traceMean = 0;
                for (int j=0; j < SMOOTHING_WINDOW_IN_FRAMES; j++){
                    traceMean += m_traceLastValues[i][j];
                }
                traceMean = traceMean / SMOOTHING_WINDOW_IN_FRAMES;
                m_traceDisplayY[i][bufNum][dataCount] = traceMean;
                m_traceDisplayT[i][bufNum][dataCount] = (timeStamp - m_softwareStartTime)/1000.0;
                m_traceNumDataInBuf[i][bufNum]++;
            }
        }
    }
}

void Miniscope::setupBNOTraceDisplay()
{
    // Setup 3 traces for BNO data

    // For BNO display ----

    // Sets color of traces
    float c0[] = {0.1,-3.0f,-3.0f};
    float c1[] = {0.5,-3.0f,-3.0f};
    float c2[] = {0.9,-3.0f,-3.0f};
    for (int i=0; i < 3; i++) {
        bnoTraceColor[0][i] = c0[i];
        bnoTraceColor[1][i] = c1[i];
        bnoTraceColor[2][i] = c2[i];
    }

    for (int i=0; i < 3; i++) {

        bnoDisplayBufNum[i] = 1;
        bnoNumDataInBuf[i][0] = 0;
        bnoNumDataInBuf[i][1] = 0;

    }
    bnoScale[0] = 1.0f/3.141592f;
    bnoScale[1] = 1.0f/3.141592f;
    bnoScale[2] = 1.0f/3.141592f;

    if (getHeadOrienataionStreamState()) {
        QJsonArray tempArray = m_ucDevice["headOrientation"].toObject()["plotTrace"].toArray();
        QString name;
        bool sameOffset;
        int count = 0;
        int idx;
        for (int i=0; i < tempArray.size(); i++) {
            name = tempArray[i].toString();
            if (name == "roll" || name == "Roll") {
                idx = 0;
            }
            else if (name == "pitch" || name == "Pitch") {
                idx = 1;
            }
            else if (name == "yaw" || name == "Yaw") {
                idx = 2;
            }
            if (count == 0)
                sameOffset = false;
            else
                sameOffset = true;

            emit addTraceDisplay(name,
                                 bnoTraceColor[idx],
                                 bnoScale[idx],
                                 "rad",
                                 sameOffset,
                                 &bnoDisplayBufNum[idx],
                                 bnoNumDataInBuf[idx],
                                 TRACE_DISPLAY_BUFFER_SIZE,
                                 bnoTraceDisplayT[idx][0],
                                 bnoTraceDisplayY[idx][0]);

            count++;
        }
    }
}

