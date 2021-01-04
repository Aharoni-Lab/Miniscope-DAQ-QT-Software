#include "behaviortracker.h"
#include "newquickview.h"
//#include "videodisplay.h"
#include "behaviortrackerworker.h"

#include <opencv2/opencv.hpp>

#include <QtQuick/QQuickItem>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QOpenGLTexture>
#include <QtGui/QOpenGLBuffer>
#include <QtGui/QOpenGLFramebufferObject>

#include <QJsonObject>
#include <QDebug>
#include <QAtomicInt>
#include <QObject>
#include <QGuiApplication>
#include <QScreen>
#include <QQuickItem>
#include <QThread>
#include <QJsonArray>
#include <QtMath>

#ifdef USE_PYTHON
 #undef slots
 #include <Python.h>
 #include <numpy/arrayobject.h>
 #define slots
#endif

BehaviorTracker::BehaviorTracker(QObject *parent, QJsonObject userConfig, qint64 softwareStartTime) :
    QObject(parent),
    numberOfCameras(0),
    m_trackingRunning(false),
    m_camResolution(640,480),
    m_btPoseCount(new QAtomicInt(0)),
    m_previousBtPoseFrameNum(0),
    usedPoses(new QSemaphore()),
    freePoses(new QSemaphore()),
    m_pCutoffDisplay(0),
    m_softwareStartTime(softwareStartTime)
{
    m_numPose = 1; // Placeholder to avoid dividing by 0;
    m_occMax = 0;
    m_plotOcc = false;
    tracesSetup = false;
    m_numTraces = 0;

    freePoses->release(POSE_BUFFER_SIZE);

    m_userConfig = userConfig;
    parseUserConfigTracker();


    behavTrackWorker = new BehaviorTrackerWorker(NULL, m_userConfig["behaviorTracker"].toObject());
    behavTrackWorker->setPoseBufferParameters(poseBuffer, poseFrameNumBuffer, POSE_BUFFER_SIZE, m_btPoseCount, freePoses, usedPoses);
    workerThread = new QThread();



}

int BehaviorTracker::initNumpy()
{
    import_array1(-1);
}

void BehaviorTracker::parseUserConfigTracker()
{
    m_btConfig = m_userConfig["behaviorTracker"].toObject();
//    QJsonObject jTracker = m_userConfig["behaviorTracker"].toObject();
//    m_trackerType = jTracker["type"].toString("None");
    m_pCutoffDisplay = m_btConfig["pCutoffDisplay"].toDouble(0);

    if (m_btConfig.contains("occupancyPlot")) {
        if (m_btConfig["occupancyPlot"].toObject()["enabled"].toBool(true)) {
            m_plotOcc = true;
            m_occNumBinsX = m_btConfig["occupancyPlot"].toObject()["numBinX"].toInt(20);
            m_occNumBinsY = m_btConfig["occupancyPlot"].toObject()["numBinY"].toInt(20);
            QJsonArray tempArray = m_btConfig["occupancyPlot"].toObject()["poseIdxToUse"].toArray();

            for (int i=0; i< tempArray.size(); i++) {
                m_poseIdxUsed.append(tempArray[i].toInt());
            }

            // Create 2D his matrix
            m_occupancy = new cv::Mat(m_occNumBinsX, m_occNumBinsY, CV_8UC3, cv::Scalar(0,0,0));

        }
    }

    // For pose Overlay
    if (m_btConfig.contains("poseOverlay")) {
        m_poseOverlayEnabled = m_btConfig["poseOverlay"].toObject()["enabled"].toBool(true);
        m_overlayType = m_btConfig["poseOverlay"].toObject()["type"].toString("point");
        m_overlayNumPoses = m_btConfig["poseOverlay"].toObject()["numOfPastPoses"].toInt(0) + 1;
        m_poseMarkerSize = m_btConfig["poseOverlay"].toObject()["markerSize"].toDouble(10);
        if (m_poseOverlayEnabled = m_btConfig["poseOverlay"].toObject().contains("skeleton")) {
            QJsonObject tempSk = m_btConfig["poseOverlay"].toObject()["skeleton"].toObject();
            m_poseOverlaySkeletonEnabled = tempSk["enabled"].toBool(true);
            QJsonArray tempArray = tempSk["connectedIndices"].toArray();
            QJsonArray connectedArray;
            for (int i=0; i < tempArray.size(); i++) {
                connectedArray = tempArray[i].toArray();
                for (int j=0; j < connectedArray.size(); j++) {
                    overlaySkeleton.append(overlayData_t());
                    overlaySkeleton.last().position[2] = 0.25f;
                    overlaySkeleton.last().poseIdx = connectedArray[j].toInt(0);
                    overlaySkeleton.last().index = i;
                }
            }
        }
    }
    else {
        m_poseOverlayEnabled = false;
        m_overlayType = "point";
        m_overlayNumPoses = 1;
    }
    // Make sure num poses is at at least a minimum
    if (m_overlayType == "ribbon" && m_overlayNumPoses < 2)
        m_overlayNumPoses = 2;
    if (m_overlayType == "line" && m_overlayNumPoses < 2)
        m_overlayNumPoses = 2;
    if (m_overlayType == "point" && m_overlayNumPoses < 1)
        m_overlayNumPoses = 1;

    if (m_poseMarkerSize < 3)
        m_poseMarkerSize = 3;
}

void BehaviorTracker::setBehaviorCamBufferParameters(QString name, qint64* timeBuf, cv::Mat *frameBuf, int bufSize, QAtomicInt *acqFrameNum)
{
    frameBuffer[name] = frameBuf;
    timeStampBuffer[name] = timeBuf;
    bufferSize[name] = bufSize;
    m_acqFrameNum[name] = acqFrameNum;

    currentFrameNumberProcessed[name] = 0;
    numberOfCameras++;

    // Set values/pointers for worker too
    behavTrackWorker->setParameters(name, frameBuf, bufSize, acqFrameNum);

}

void BehaviorTracker::setupDisplayTraces()
{
    QString tempS;
    int idx;
    if (m_btConfig.contains("poseIdxForTraceDisplay")) {
        QJsonArray tempArray = m_btConfig["poseIdxForTraceDisplay"].toArray();
        for (int i=0; i<tempArray.size();i++) {
            tempS = tempArray[i].toString();
            if (tempS.endsWith("wh",Qt::CaseInsensitive) || tempS.endsWith("hw",Qt::CaseInsensitive)) {
                tempS.chop(2);
                idx = tempS.toInt();
                if (idx >= 0) {
                    handleAddNewTracePose(idx, "w", false);
                    handleAddNewTracePose(idx, "h", true);
                }
            }
            else if (tempS.endsWith("w",Qt::CaseInsensitive)) {
                tempS.chop(1);
                idx = tempS.toInt();
                if (idx >= 0) {
                    handleAddNewTracePose(idx, "w", false);
                }
            }
            else if (tempS.endsWith("h",Qt::CaseInsensitive)) {
                tempS.chop(1);
                idx = tempS.toInt();
                if (idx >= 0) {
                    handleAddNewTracePose(idx, "h", false);
                }
            }
            else {
                // Something is wrong with how these are defined in user config
                sendMessage("WARNING: Your \"poseIdxForTraceDisplay\" is incorrect for " + tempS + ". Should be an integer followed by \"w\", \"h\", or \"wh\".");
            }


        }

    }
}

void BehaviorTracker::cameraCalibration()
{
    // calibrate differently for 1 and 2 cameras
    // for 1 camera need to define points on track and use prospective
    // for 2 cameras we can use stereo calibrate and find 3d point
    // ask user for specs of calibration grid (or just use a standard one
    // collect images in increments of a few seconds
    // display if calibration grid was detected
    // run calibration and save to file(s)
}

void BehaviorTracker::createView(QSize resolution)
{
    m_camResolution = resolution;
    QJsonObject btConfig = m_userConfig["behaviorTracker"].toObject();

    qmlRegisterType<TrackerDisplay>("TrackerDisplay", 1, 0, "TrackerDisplay");
    const QUrl url("qrc:/behaviorTracker.qml");
    view = new NewQuickView(url);

    // TODO: Probably should grab this from the behavior cam size...


    view->setWidth(resolution.width() * btConfig["windowScale"].toDouble(1));
    view->setHeight(resolution.height() * btConfig["windowScale"].toDouble(1));

    view->setTitle("Behavior Tracker");
    view->setX(btConfig["windowX"].toInt(1));
    view->setY(btConfig["windowY"].toInt(1));

#ifdef Q_OS_WINDOWS
    view->setFlags(Qt::Window | Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint);
#endif


    rootObject = view->rootObject();
    trackerDisplay = rootObject->findChild<TrackerDisplay*>("trackerDisplay");
    QObject::connect(trackerDisplay->window(), &QQuickWindow::beforeRendering, this, &BehaviorTracker::sendNewFrame);
    trackerDisplay->setPValueCutOff(m_pCutoffDisplay);
    trackerDisplay->setOverlayShowState(m_poseOverlayEnabled);
    trackerDisplay->setPoseMarkerSize(m_poseMarkerSize);
    trackerDisplay->setOverlaySkeletonShowState(m_poseOverlaySkeletonEnabled);

    if (m_plotOcc) {
        trackerDisplay->setShowOccState(true);
        rootObject->findChild<QObject*>("occRect")->setProperty("visible", m_plotOcc);
    }

    view->show();
}

void BehaviorTracker::connectSnS()
{

}

void BehaviorTracker::setUpDLCLive()
{


}

void BehaviorTracker::startThread()
{
    //TODO: Stop thread and working if missing path to py env.
    if (m_userConfig["behaviorTracker"].toObject().contains("pyEnvPath")) {
        sendMessage("Using \"" + m_userConfig["behaviorTracker"].toObject()["pyEnvPath"].toString() + "\" as Python Environment.");

        behavTrackWorker->moveToThread(workerThread);

        QObject::connect(workerThread, SIGNAL (started()), behavTrackWorker, SLOT (startRunning()));
        QObject::connect(this, SIGNAL( closeWorker()), behavTrackWorker, SLOT (close()));
        QObject::connect(behavTrackWorker, &BehaviorTrackerWorker::sendMessage, this, &BehaviorTracker::sendMessage);

        workerThread->start();
    }
    else
        // Py environment path missing
        sendMessage("Error: Path to Python Environment (\"pyEnvPath:\") missing from user config file!");


}

void BehaviorTracker::makeRibbon()
{
    double slope;
    double dx, dy;
    double startingWidth = m_poseMarkerSize / (float)(m_camResolution.width());
    double width = 0;
    double sqrtTerm;
    int previousIdx = -1;
    int count = 0;

//    if (overlayRibbon.isEmpty()) {
//        // fill up overlyRibbon by making twice the size of overlayData;
//        overlayRibbon = overlayLine;
//        overlayRibbon.append(overlayLine);
//    }

    for (int i=0; i<overlayLine.length(); i++) {
        if (overlayLine[i].index != previousIdx) {
            // first of the pose lines
            previousIdx = overlayLine[i].index;
            width = 0;
            count = 0;
        }

        if (count == 0) {
            // First
            slope = (overlayLine[i + 1].position[0] - overlayLine[i].position[0]) /
                    (overlayLine[i + 1].position[1] - overlayLine[i].position[1]);

        }
        else if (count < (m_overlayNumPoses - 1)) {
            // All middle points
            slope = (overlayLine[i + 1].position[0] - overlayLine[i - 1].position[0]) /
                    (overlayLine[i + 1].position[1] - overlayLine[i - 1].position[1]);

        }
        else {
            // Last
            slope = (overlayLine[i].position[0] - overlayLine[i - 1].position[0]) /
                    (overlayLine[i].position[1] - overlayLine[i - 1].position[1]);

        }
        sqrtTerm = qSqrt(1 + slope*slope);
        dx = width / sqrtTerm;
        dy = slope * dx;

        overlayRibbon[2 * i] = overlayLine[i];
        overlayRibbon[2 * i].position[0] -= dx;
        overlayRibbon[2 * i].position[1] -= dy;

        overlayRibbon[(2 * i) + 1] = overlayLine[i];
        overlayRibbon[(2 * i) + 1].position[0] += dx;
        overlayRibbon[(2 * i) + 1].position[1] += dy;

        width += startingWidth/(float)m_overlayNumPoses;
        count++;
    }
}

void BehaviorTracker::sendNewFrame()
{
    // TODO: currently writen to handle only 1

    int poseNum = *m_btPoseCount;
    if (poseNum > m_previousBtPoseFrameNum) {
//        qDebug() << "send New Frame";
        m_previousBtPoseFrameNum = poseNum;
        cv::Mat cvFrame;
        QImage qFrame;

        int frameNum = poseFrameNumBuffer[(poseNum - 1) % POSE_BUFFER_SIZE];
        int frameIdx = frameNum % bufferSize.first();

        qint64 timeStamp = timeStampBuffer.first()[frameIdx];
        if (frameBuffer.first()[frameIdx].channels() == 1) {
            cv::cvtColor(frameBuffer.first()[frameIdx], cvFrame, cv::COLOR_GRAY2BGR);
        }
        else {
            cvFrame = frameBuffer.first()[frameIdx].clone();

        }

        // TODO: Shouldn't hardcore pose vector size/shape
        QVector<float> tempPose = poseBuffer[(poseNum - 1) % POSE_BUFFER_SIZE];
        m_numPose = tempPose.length()/3;
        QVector<QVector3D> pose;
        for (int i = 0; i < m_numPose; i++) {
            pose.append(QVector3D(tempPose[i + 0], tempPose[i + m_numPose], tempPose[i + 2 * m_numPose]));
            // TODO: Change to our own shader to plot points
//            if (pose.last().z() > m_pCutoffDisplay)
//                cv::circle(cvFrame, cv::Point(pose.last().x(),pose.last().y()),5,cv::Scalar(colors[i*3 + 2]*255,colors[i*3+1]*255,colors[i*3+0]*255),cv::FILLED);
//        }
        }

        // For Tracker Overlay
        if (m_poseOverlayEnabled) {
            // Needs to be called after knowing the number of poses
            if (tracesSetup == false && m_numPose > 0) {
                setupDisplayTraces();
                tracesSetup = true;
            }
            // init overlayData
            if (overlayLine.length() != (m_overlayNumPoses * pose.length())) {
                overlayPose.clear();
                overlayLine.clear();
                overlayRibbon.clear();
                for (int i=0; i < pose.length(); i++) {
                    overlayPose.append(overlayData_t());
                    for (int j=0; j < m_overlayNumPoses; j++) {
                        overlayLine.append(overlayData_t());

                        overlayRibbon.append(overlayData_t());
                        overlayRibbon.append(overlayData_t());
                    }
                }
            }
            // Move and fill overlayData vector
            for (int i=0; i < pose.length(); i++) {
                for (int j=0; j < (m_overlayNumPoses - 1); j++) {
                    // Shift old data down 1
                    overlayLine[i * m_overlayNumPoses + j] = overlayLine[i * m_overlayNumPoses + j + 1];
                    overlayLine[i * m_overlayNumPoses + j].position[2] -= 1.0f/m_overlayNumPoses;
    //                if (i == 0) {
    //                    qDebug() << j << overlayData[i * m_overlayNumPoses + j].position[0] << overlayData[i * m_overlayNumPoses + j].position[1];
    //                }
                }
                // Add new data
                overlayLine[((i + 1) * m_overlayNumPoses) - 1].position[0] = 2.0f * pose[i].x()/m_camResolution.width() - 1.0f;
                overlayLine[((i + 1) * m_overlayNumPoses) - 1].position[1] = -2.0f * pose[i].y()/m_camResolution.height() + 1.0f;
                overlayLine[((i + 1) * m_overlayNumPoses) - 1].position[2] = 1.0f;
                overlayLine[((i + 1) * m_overlayNumPoses) - 1].color = (float)(i)/(float)(pose.length());
                overlayLine[((i + 1) * m_overlayNumPoses) - 1].index = (float)i;
                overlayLine[((i + 1) * m_overlayNumPoses) - 1].pValue = pose[i].z();

                overlayPose[i] = overlayLine[((i + 1) * m_overlayNumPoses) - 1];

            }
            if (m_poseOverlaySkeletonEnabled) {
                int idx;
                for (int i=0; i < overlaySkeleton.length(); i++) {
                    idx = overlaySkeleton[i].poseIdx;
                    overlaySkeleton[i].position[0] = overlayPose[idx].position[0];
                    overlaySkeleton[i].position[1] = overlayPose[idx].position[1];
                    overlaySkeleton[i].color = 2; //overlayPose[idx].color;
                    overlaySkeleton[i].pValue = overlayPose[idx].pValue;
                }
                trackerDisplay->setSkeletonData(overlaySkeleton);
            }
            if (m_overlayType == "point")
                trackerDisplay->setOverlayData(overlayLine, m_overlayType);
            else if (m_overlayType == "line")
                trackerDisplay->setOverlayData(overlayLine, m_overlayType);
            else if (m_overlayType == "ribbon") {
                makeRibbon();
                trackerDisplay->setOverlayData(overlayRibbon, m_overlayType);
            }
        }


        // For Occupancy plot
        if (m_plotOcc) {
            int tempX = 0;
            int tempY = 0;
            int count = 0;
            int idx;
            int tempVal;
            cv::Vec3b tempValues;
            for (int i=0; i < m_poseIdxUsed.length(); i++) {
                idx = m_poseIdxUsed[i];
                if (pose[idx].z() > m_pCutoffDisplay) {
                    tempX += pose[idx].x();
                    tempY += pose[idx].y();
                    count++;
                }
            }
            if (count > 0) {
                tempX = (tempX/count) * m_occNumBinsX / m_camResolution.width();
                tempY = (tempY/count) * m_occNumBinsY / m_camResolution.height();
//                qDebug() << tempX << tempY;
                tempValues = m_occupancy->at<cv::Vec3b>(tempY, tempX);
                tempVal = tempValues[0] + tempValues[1] * 256; // TODO: add last index with 2^16
//                qDebug() << tempVal;
                if (tempVal > m_occMax) {
                    m_occMax = tempVal;
                    trackerDisplay->setOccMax(m_occMax);
                }

//                qDebug() << m_occupancy->channels() << m_occupancy->rows << m_occupancy->cols;
                if (tempValues[0] < 254) {
                    tempValues[0] += 1;
                }
                else {
                    // TODO: Handle third color as well
                    tempValues[0] = 0;
                    tempValues[1] += 1;
                }
                m_occupancy->at<cv::Vec3b>(tempY, tempX) = tempValues;
//                cv::Mat tempFrame;
//                cv::cvtColor(*m_occupancy, tempFrame, cv::COLOR_GRAY2BGR);
//                trackerDisplay->setDisplayOcc(QImage(tempFrame.data, tempFrame.cols, tempFrame.rows, tempFrame.step, QImage::Format_RGB888));
                trackerDisplay->setDisplayOcc(QImage(m_occupancy->data, m_occupancy->cols, m_occupancy->rows, m_occupancy->step, QImage::Format_RGB888));
            }
        }
        qFrame = QImage(cvFrame.data, cvFrame.cols, cvFrame.rows, cvFrame.step, QImage::Format_RGB888);
        trackerDisplay->setDisplayImage(qFrame);

        // For trace display
        if (m_numTraces > 0) {
            int bufNum;
            int dataCount;

            for (int i=0; i < m_numTraces; i++) {
                if (m_traceDisplayBufNum[i] == 0)
                    bufNum = 1;
                else
                    bufNum = 0;
                dataCount  = m_traceNumDataInBuf[i][bufNum];
                if (dataCount < TRACE_DISPLAY_BUFFER_SIZE) {
                    // There is space for more data

                    m_traceDisplayY[i][bufNum][dataCount] = pose[m_tracePoseIdx[i]][m_tracePoseType[i]] - m_traceWindowLength[i]/2.0f;
                    if (pose[m_tracePoseIdx[i]][2] > m_pCutoffDisplay)
                        m_traceDisplayT[i][bufNum][dataCount] = (timeStamp - m_softwareStartTime)/1000.0;
                    else
                        m_traceDisplayT[i][bufNum][dataCount] = -1;
                    m_traceNumDataInBuf[i][bufNum]++;
                }
            }
        }
    }
}

void BehaviorTracker::startRunning()
{

}
void BehaviorTracker::close()
{
    emit closeWorker();
    //    view->close();
}

void BehaviorTracker::handleAddNewTracePose(int poseIdx, QString type, bool sameOffset)
{
//    m_traceColors[0][0] = &colors[0];
    if ((m_numTraces + 1) < NUM_MAX_POSE_TRACES) {
        if (type == "w") {
            m_tracePoseIdx[m_numTraces] = poseIdx;

            m_tracePoseType[m_numTraces] = 0;
            m_traceWindowLength[m_numTraces] = (float)m_camResolution.width();
        }
        else {
            m_tracePoseIdx[m_numTraces] = poseIdx;
            m_tracePoseType[m_numTraces] = 1;
            m_traceWindowLength[m_numTraces] = (float)m_camResolution.height();
        }
//        m_traceColors[m_numTraces][0] = 1.0f;//((float)colors[poseIdx*3 + 0])/100.0f;
//        m_traceColors[m_numTraces][1] = 1.0f;//((float)colors[poseIdx*3 + 1])/100.0f;
//        m_traceColors[m_numTraces][2] = 1.0f;//((float)colors[poseIdx*3 + 2])/100.0f;

        m_traceDisplayBufNum[m_numTraces]   = 1;
        m_traceNumDataInBuf[m_numTraces][0] = 0;
        m_traceNumDataInBuf[m_numTraces][1] = 0;

        colors[m_numTraces][0] = (float)poseIdx/(float)m_numPose;

        if (sameOffset) {
            if (colors[m_numTraces][0] < 0.9)
                colors[m_numTraces][0] =  (float)poseIdx/(float)m_numPose + 0.1f;
            else
                colors[m_numTraces][0] =  (float)poseIdx/(float)m_numPose - 0.1f;
        }

        colors[m_numTraces][1] = -3.0f;
        colors[m_numTraces][2] = -3.0f;
        emit addTraceDisplay("Joint" + QString::number(poseIdx) + type,
                             colors[m_numTraces],
                             2.0/m_traceWindowLength[m_numTraces],
                             "px",
                             sameOffset,
                             &m_traceDisplayBufNum[m_numTraces],
                             m_traceNumDataInBuf[m_numTraces],
                             TRACE_DISPLAY_BUFFER_SIZE,
                             m_traceDisplayT[m_numTraces][0],
                             m_traceDisplayY[m_numTraces][0]);
        m_numTraces++;
    }

}

TrackerDisplayRenderer::TrackerDisplayRenderer(QObject *parent, QSize displayWindowSize):
    QObject(parent),
    m_t(0),
    m_newImage(false),
    m_newOccupancy(false),
    m_textureImage(nullptr),
    m_texture2DHist(nullptr),
    m_programImage(nullptr),
    m_programOccupancy(nullptr),
    m_programTrackingOverlay(nullptr)
{
    overlaySkeletonEnabled = false;
    poseMarkerSize = 3;
    pValCut = 0.0f;
    occPlotBox[0] = 0.5;
    occPlotBox[1] = 0.5;
    occPlotBox[2] = 0.4;
    occPlotBox[3] = 0.4;
    m_showOcc = false;
    m_viewportSize = displayWindowSize;
    initPrograms();
}

TrackerDisplayRenderer::~TrackerDisplayRenderer()
{
    delete m_textureImage;
    delete m_texture2DHist;
    delete m_programImage;
    delete m_programOccupancy;
    delete m_programTrackingOverlay;
}

void TrackerDisplayRenderer::initPrograms()
{
    initializeOpenGLFunctions();

    m_programImage = new QOpenGLShaderProgram();
    m_programImage->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/tracker.vert");
    m_programImage->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/tracker.frag");
    m_programImage->link();

    m_programOccupancy = new QOpenGLShaderProgram();
    m_programOccupancy->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/tracker.vert");
    m_programOccupancy->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/tracker.frag");
    m_programOccupancy->link();

    m_programTrackingOverlay = new QOpenGLShaderProgram();
    m_programTrackingOverlay->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/trackerOverlay.vert");
    m_programTrackingOverlay->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/trackerOverlay.frag");
    m_programTrackingOverlay->link();



    m_textureImage = new QOpenGLTexture(QImage(":/img/MiniscopeLogo.png").rgbSwapped());
//    m_textureImage->bind(0);
    m_texture2DHist = new QOpenGLTexture(QImage(":/img/MiniscopeLogo.png").rgbSwapped());
}

void TrackerDisplayRenderer::drawImage()
{
//    qDebug() << "DRAWING!";
    m_textureImage->bind(0);
    m_programImage->bind();



    m_programImage->setUniformValue("texture", 0);
    m_programImage->setUniformValue("u_displayType", 0.0f);

    m_programImage->enableAttributeArray("position");
    m_programImage->enableAttributeArray("texcoord");

    float position[] = {
        -1, -1,
        1, -1,
        -1, 1,
        1, 1

    };
    float texcoord[] = {
//        1, 1,
//        1, 0,
//        0, 1,
//        0, 0
        0, 1,
        1, 1,
        0, 0,
        1, 0

    };
    m_programImage->setAttributeArray("position", GL_FLOAT, position, 2);
    m_programImage->setAttributeArray("texcoord", GL_FLOAT, texcoord, 2);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_programImage->disableAttributeArray(0);
    m_programImage->disableAttributeArray(1);


    if (m_newImage) {
        m_textureImage->destroy();
        m_textureImage->create();
        m_textureImage->setData(m_displayImage);
        m_newImage = false;
    }

    m_programImage->release();
    m_textureImage->release(0);

}

void TrackerDisplayRenderer::draw2DHist()
{
    // TODO: store occupancy in frame buffer and just update a sinlge pixel on draw?
    m_texture2DHist->bind(0);
    m_programOccupancy->bind();



    m_programOccupancy->setUniformValue("texture", 0);
    m_programOccupancy->setUniformValue("u_displayType", 1.0f);
    m_programOccupancy->setUniformValue("u_occMax", (float)occMax);

    m_programOccupancy->enableAttributeArray("position");
    m_programOccupancy->enableAttributeArray("texcoord");

    float position[] = {
        occPlotBox[0], occPlotBox[1],
        occPlotBox[0] + occPlotBox[2], occPlotBox[1],
        occPlotBox[0],  occPlotBox[1] + occPlotBox[3],
        occPlotBox[0] + occPlotBox[2], occPlotBox[1] + occPlotBox[3]

    };
    float texcoord[] = {
//        1, 1,
//        1, 0,
//        0, 1,
//        0, 0
        0, 1,
        1, 1,
        0, 0,
        1, 0

    };
    m_programOccupancy->setAttributeArray("position", GL_FLOAT, position, 2);
    m_programOccupancy->setAttributeArray("texcoord", GL_FLOAT, texcoord, 2);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_programOccupancy->disableAttributeArray(0);
    m_programOccupancy->disableAttributeArray(1);


    if (m_newOccupancy) {
        m_texture2DHist->destroy();
        m_texture2DHist->create();
        m_texture2DHist->setData(m_displayOcc);
        m_newOccupancy = false;
    }

    m_programOccupancy->release();
    m_texture2DHist->release(0);

}

void TrackerDisplayRenderer::drawTrackerOverlay (QString type) {

    // TODO: Handling the different data and skeleton types way better!!!
    if (!overlayDataVOB.isCreated() && !overlayData.isEmpty() && type == "data") {
        // Setup VOB
        overlayDataVOB.create();
        overlayDataVOB.bind();
        overlayDataVOB.allocate(&overlayData[0], sizeof(overlayData_t) * overlayData.length());
        overlayDataVOB.release();
    }

    if (!overlaySkeletonVOB.isCreated() && !overlaySkeletonData.isEmpty() && type == "skeleton") {
        // Setup VOB
        overlaySkeletonVOB.create();
        overlaySkeletonVOB.bind();
        overlaySkeletonVOB.allocate(&overlaySkeletonData[0], sizeof(overlayData_t) * overlaySkeletonData.length());
        overlaySkeletonVOB.release();
    }

    bool vobBound = false;
    m_programTrackingOverlay->bind();

    if (overlayDataVOB.isCreated() && type == "data") {
        overlayDataVOB.bind();
        overlayDataVOB.allocate(&overlayData[0], sizeof(overlayData_t) * overlayData.length());
        vobBound = true;
    }
    else if (overlaySkeletonVOB.isCreated() && type == "skeleton") {
        overlaySkeletonVOB.bind();
        overlaySkeletonVOB.allocate(&overlaySkeletonData[0], sizeof(overlayData_t) * overlaySkeletonData.length());
        vobBound = true;
    }
    if (vobBound) {
        m_programTrackingOverlay->setUniformValue("u_pValueCutoff", pValCut);

        m_programTrackingOverlay->enableAttributeArray("a_position");
        m_programTrackingOverlay->enableAttributeArray("a_color");
        m_programTrackingOverlay->enableAttributeArray("a_index");
        m_programTrackingOverlay->enableAttributeArray("a_pValue");

        m_programTrackingOverlay->setAttributeBuffer("a_position",GL_FLOAT, 0,                3, sizeof(overlayData_t));
        m_programTrackingOverlay->setAttributeBuffer("a_color", GL_FLOAT, 3 * sizeof(float), 1, sizeof(overlayData_t));
        m_programTrackingOverlay->setAttributeBuffer("a_index", GL_FLOAT, 4 * sizeof(float), 1, sizeof(overlayData_t));
        m_programTrackingOverlay->setAttributeBuffer("a_pValue", GL_FLOAT, 5 * sizeof(float), 1, sizeof(overlayData_t));


        if (type == "data") {
            if (overlayType == "point") {
                m_programTrackingOverlay->setUniformValue("u_pointSize", (float)poseMarkerSize);
                glDrawArrays(GL_POINTS, 0, overlayDataVOB.size()/(sizeof(overlayData_t)));

            }
            else if (overlayType == "line") {
                glLineWidth(poseMarkerSize);
                glDrawArrays(GL_LINE_STRIP, 0, overlayDataVOB.size()/(sizeof(overlayData_t)));
            }
            else if (overlayType == "ribbon") {
                glDrawArrays(GL_TRIANGLE_STRIP, 0, overlayDataVOB.size()/(sizeof(overlayData_t)));
            }
        }
        else if (type == "skeleton") {
            glLineWidth(poseMarkerSize/2);
            glDrawArrays(GL_LINE_STRIP, 0, overlaySkeletonVOB.size()/(sizeof(overlayData_t)));
        }


        m_programTrackingOverlay->disableAttributeArray("a_position");
        m_programTrackingOverlay->disableAttributeArray("a_color");
        m_programTrackingOverlay->disableAttributeArray("a_index");
        m_programTrackingOverlay->disableAttributeArray("a_pValue");

        if (type == "data")
            overlayDataVOB.release();
        else if (type == "skeleton")
            overlaySkeletonVOB.release();

        m_programTrackingOverlay->release();
    }
}

void TrackerDisplayRenderer::paint()
{
    glViewport(0, 0, m_viewportSize.width(), m_viewportSize.height());
    glDisable(GL_DEPTH_TEST);

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_PROGRAM_POINT_SIZE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    drawImage();

//    glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
//    glBlendFunc(GL_SRC_ALPHA, GL_ONE);


    if (overlayEnabled) {
        if (overlaySkeletonEnabled) {
            drawTrackerOverlay("skeleton");
        }
        drawTrackerOverlay("data");
    }

    if( m_showOcc) {
//        glDisable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        draw2DHist();
    }

    // Not strictly needed for this example, but generally useful for when
    // mixing with raw OpenGL.
    m_window->resetOpenGLState();
}

TrackerDisplay::TrackerDisplay():
    m_t(0),
    m_renderer(nullptr)
{
    m_showOcc = false;
    m_overlayEnabled = false;
    m_pValCut = 0.0f;
    connect(this, &QQuickItem::windowChanged, this, &TrackerDisplay::handleWindowChanged);

}


void TrackerDisplay::occRectChanged(float x, float y, float w, float h)
{
//    qDebug() << "HEHRE!!" << s;
    if (m_renderer) {
        m_renderer->occPlotBox[0] = x;
        m_renderer->occPlotBox[1] = y;
        m_renderer->occPlotBox[2] = w;
        m_renderer->occPlotBox[3] = h;
    }
//    qDebug() << x << y << w << h;
}

void TrackerDisplay::setT(qreal t)
{
    if (t == m_t)
        return;
    m_t = t;
    emit tChanged();
    if (window())
        window()->update();
}

void TrackerDisplay::setDisplayImage(QImage image)
{
    if (m_renderer && !m_renderer->m_newImage)
        m_renderer->setDisplayImage(image);
}

void TrackerDisplay::setDisplayOcc(QImage image)
{
    if (m_renderer && !m_renderer->m_newOccupancy)
        m_renderer->setDisplayOcc(image);
}

void TrackerDisplay::setOverlayData(QVector<overlayData_t> data, QString type)
{
    if (m_renderer) {
        m_renderer->overlayData = data;
        m_renderer->overlayType = type;
    }

}

void TrackerDisplay::setSkeletonData(QVector<overlayData_t> data)
{
    if (m_renderer)
        m_renderer->overlaySkeletonData = data;
//    qDebug() << data[0].position[0] << data[0].position[0] << data.length();
}

void TrackerDisplay::setShowOccState(bool state)
{
    m_showOcc = state;

}

void TrackerDisplay::setOverlayShowState(bool state)
{
    m_overlayEnabled = state;
}

void TrackerDisplay::setOverlaySkeletonShowState(bool state)
{
    m_overlaySkeletonEnabled = state;
}

void TrackerDisplay::handleWindowChanged(QQuickWindow *win)
{
    if (win) {
        connect(win, &QQuickWindow::beforeSynchronizing, this, &TrackerDisplay::sync, Qt::DirectConnection);
        connect(win, &QQuickWindow::sceneGraphInvalidated, this, &TrackerDisplay::cleanup, Qt::DirectConnection);
//! [1]
        // If we allow QML to do the clearing, they would clear what we paint
        // and nothing would show.
//! [3]
        win->setClearBeforeRendering(false);
    }
}

void TrackerDisplay::sync()
{
    if (!m_renderer) {
        m_renderer = new TrackerDisplayRenderer(nullptr, window()->size() * window()->devicePixelRatio());
//        m_renderer->setShowSaturation(m_showSaturation);
//        m_renderer->setDisplayFrame(QImage("C:/Users/DBAharoni/Pictures/Miniscope/Logo/1.png"));
        connect(window(), &QQuickWindow::beforeRendering, m_renderer, &TrackerDisplayRenderer::paint, Qt::DirectConnection);
        m_renderer->m_showOcc = m_showOcc;
        m_renderer->pValCut = m_pValCut;
        m_renderer->overlayEnabled = m_overlayEnabled;
        m_renderer->poseMarkerSize = m_poseMarkerSize;
        m_renderer->overlaySkeletonEnabled = m_overlaySkeletonEnabled;
    }
    m_renderer->setViewportSize(window()->size() * window()->devicePixelRatio());
//    m_renderer->setT(m_t);
//    m_renderer->setDisplayFrame(m_displayFrame);
    m_renderer->setWindow(window());
}

void TrackerDisplay::cleanup()
{
    if (m_renderer) {
        delete m_renderer;
        m_renderer = nullptr;
    }
}
