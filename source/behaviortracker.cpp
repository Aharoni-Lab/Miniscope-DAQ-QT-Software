#include "behaviortracker.h"
#include "newquickview.h"

#include <opencv2/opencv.hpp>

#include <QJsonObject>
#include <QDebug>
#include <QAtomicInt>
#include <QObject>
#include <QGuiApplication>
#include <QScreen>
#include <QQuickItem>

#ifdef USE_PYTHON
 #undef slots
 #include <Python.h>
 #include <numpy/arrayobject.h>
 #define slots
#endif

BehaviorTracker::BehaviorTracker(QObject *parent, QJsonObject userConfig) :
    QObject(parent),
    numberOfCameras(0),
    m_trackingRunning(false)
{
    m_userConfig = userConfig;
    parseUserConfigTracker();

#ifdef USE_PYTHON
    // TODO: Set parameters in user config file
    Py_SetPythonHome(L"C:/Users/dbaha/.conda/envs/dlc-live");
    Py_Initialize();
#endif

    setUpDLCLive();
    // Will try using DLC's viewer before using our own
//    createView();
}

void BehaviorTracker::parseUserConfigTracker()
{
//    QJsonObject jTracker = m_userConfig["behaviorTracker"].toObject();
//    m_trackerType = jTracker["type"].toString("None");

}

void BehaviorTracker::setBehaviorCamBufferParameters(QString name, cv::Mat *frameBuf, int bufSize, QAtomicInt *acqFrameNum)
{
    frameBuffer[name] = frameBuf;
    bufferSize[name] = bufSize;
    m_acqFrameNum[name] = acqFrameNum;

    currentFrameNumberProcessed[name] = 0;
    numberOfCameras++;
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

void BehaviorTracker::createView()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect  screenGeometry = screen->geometry();
    int height = screenGeometry.height();
    int width = screenGeometry.width();

    const QUrl url(QStringLiteral("qrc:/behaviorTracker.qml"));
    view = new NewQuickView(url);

    view->setWidth(400);
    view->setHeight(200);
    view->setTitle("Behavior Tracker");
    view->setX(400);
    view->setY(50);
    view->show();

    rootObject = view->rootObject();
//    messageTextArea = rootObject->findChild<QQuickItem*>("messageTextArea");

    connectSnS();
}

void BehaviorTracker::connectSnS()
{

}

void BehaviorTracker::setUpDLCLive()
{

#ifdef USE_PYTHON
    PyObject *pName, *pModule, *pFunc;
    PyObject *pArgs, *pValue;


    // Adds .exe's directory to path to find py file
    PyObject* sysPath = PySys_GetObject((char*)"path");
    // TODO: Don't hard code this directory!
    PyObject* programName = PyUnicode_FromString("C:/Users/dbaha/Documents/Projects/Miniscope-DAQ-QT-Software/source/");
    PyList_Append(sysPath, programName);
    Py_DECREF(programName);
    Py_DECREF(sysPath);

    // Setup module and function(s)
    pName = PyUnicode_DecodeFSDefault("pythonTest");
    pModule = PyImport_Import(pName);
    Py_DECREF(pName);
    if (pModule != NULL) {
        pFunc = PyObject_GetAttrString(pModule, "setModelPath");
        if (pFunc && PyCallable_Check(pFunc)) {
            pArgs = PyTuple_New(2); // Number of args for function args
            pValue = PyUnicode_FromString(m_userConfig["modelPath"].toString().toUtf8());
            PyTuple_SetItem(pArgs, 0, pValue);

            pValue = PyFloat_FromDouble(m_userConfig["resize"].toDouble(1));
            PyTuple_SetItem(pArgs, 1, pValue);

            pValue = PyObject_CallObject(pFunc, pArgs);
            Py_DECREF(pArgs);
            if (pValue != NULL) {
                qDebug() << "Result of call:" << PyLong_AsLong(pValue);
                Py_DECREF(pValue);
            }
            else {
                Py_DECREF(pFunc);
                Py_DECREF(pModule);
                qDebug() << "Call failed";
            }
        }
        else {
            if (PyErr_Occurred())
                PyErr_Print();
            qDebug() << "Cannot find function";
        }
        Py_XDECREF(pFunc);
        Py_DECREF(pModule);
    }
    else {
        PyErr_Print();
        qDebug() << "Failed to load";
    }

#endif
}

QList<double> BehaviorTracker::getDLCLivePose(cv::Mat frame)
{
    QList<double> returnPose;
#ifdef USE_PYTHON
//    import_array()
    qDebug() << "Frame" << frame.cols << frame.rows;

#endif
    return returnPose;
}

void BehaviorTracker::startRunning()
{
    // Gets called when thread starts running
    m_trackingRunning = true;
    int acqFrameNum;
    int frameIdx;
    QList<QString> camNames = frameBuffer.keys();
    while (m_trackingRunning) {
        for (int camNum = 0; camNum < camNames.length(); camNum++) {
            // Loops through cameras to see if new frames are ready
            acqFrameNum = *m_acqFrameNum[camNames[camNum]];
            if (acqFrameNum > currentFrameNumberProcessed[camNames[camNum]]) {
                // New frame ready for behavior tracking
                frameIdx = (acqFrameNum - 1) % bufferSize[camNames[camNum]];
                getDLCLivePose(frameBuffer[camNames[camNum]][frameIdx]);
                currentFrameNumberProcessed[camNames[camNum]] = acqFrameNum;
            }
        }
    }

}
void BehaviorTracker::close()
{
    Py_Finalize();
//    view->close();
}
