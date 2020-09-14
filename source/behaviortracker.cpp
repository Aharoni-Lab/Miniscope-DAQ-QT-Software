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
#include <QThread>

#ifdef USE_PYTHON
 #undef slots
 #include <Python.h>
 #include <numpy/arrayobject.h>
 #define slots
#endif

BehaviorTracker::BehaviorTracker(QObject *parent, QJsonObject userConfig) :
    QObject(parent),
    numberOfCameras(0),
    m_trackingRunning(false),
    m_DLCInitInfDone(false)
{
    m_userConfig = userConfig;
    parseUserConfigTracker();


//    createView();
}

int BehaviorTracker::initNumpy()
{
    import_array1(-1);
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
    PyObject *pName;

    pName = PyUnicode_DecodeFSDefault("pythonTest");
    pModule = PyImport_Import(pName);
    Py_DECREF(pName);
    pDict = PyModule_GetDict(pModule);

    pClass = PyDict_GetItemString(pDict, "MiniDLC");

    pArgs = PyTuple_New(2);
    pValue = PyUnicode_FromString(m_userConfig["modelPath"].toString().toUtf8());
    PyTuple_SetItem(pArgs, 0, pValue);

    pValue = PyFloat_FromDouble(m_userConfig["resize"].toDouble(1));
    PyTuple_SetItem(pArgs, 1, pValue);

    pInstance = PyObject_CallObject(pClass,pArgs);

//    pValue = PyObject_CallMethod(pInstance,"initInference",NULL);

//    Py_DECREF(pValue);
//    Py_DECREF(pArgs);
//    Py_DECREF(pClass);
//    Py_DECREF(pDict);
//    Py_DECREF(pModule);
/*
    PyObject *pName, *pModule, *pFunc;
    PyObject *pArgs, *pValue;

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

    */
#endif
}

QList<double> BehaviorTracker::getDLCLivePose(cv::Mat frame)
{
    qDebug() << "In getDLCPose_C";
    QList<double> returnPose;

#ifdef USE_PYTHON
    PyObject *mat;

    uchar* m = frame.ptr(0);
    npy_intp mdim[] = { frame.rows, frame.cols, frame.channels()};

    if (frame.channels() == 1)
        mat = PyArray_SimpleNewFromData(2, mdim, NPY_UINT8, m);
    else
        mat = PyArray_SimpleNewFromData(3, mdim, NPY_UINT8, m);

//    pArgs = PyTuple_New (1);
//    PyTuple_SetItem (pArgs, 0, mat);
//    pArgs = Py_BuildValue("(O)", mat);

//    pValue = PyObject_CallMethod(pInstance,"sayHi",NULL);

    if (m_DLCInitInfDone == false) {
        pValue = PyObject_CallMethod(pInstance,"initInference", "(O)", mat);
        m_DLCInitInfDone = true;
    }
    else
        pValue = PyObject_CallMethod(pInstance,"getPose", "(O)", mat);

    PyArrayObject *np_ret = reinterpret_cast<PyArrayObject*>(pValue);

    // Convert back to C++ array and print.

    npy_intp *arraySize = PyArray_SHAPE(np_ret);
    QVector<QVector<float>> pose(arraySize[0], QVector<float>(arraySize[1]));
    float *c_out;
    c_out = reinterpret_cast<float*>(PyArray_DATA(np_ret));
    for (int i = 0; i < arraySize[1]; i++){
        for (int j = 0; j < arraySize[0]; j++) {
            pose[j][i] = c_out[i * arraySize[0] + j];
        }
    }
    qDebug() << pose;
    QThread::msleep(500);



//    Py_DECREF(pValue);
    Py_DECREF(mat);
//    Py_DECREF(pArgs);




//    // Handle converstion from cv:Mat to numpy array
//    int numChannels = frame.channels();
////    int numElem = frame.cols * frame.rows * numChannels;

//    // TODO: Maybe make a copy of the frame to avoid any pointer/memory issues
//    uchar temp[300] = {0};
//    uchar* m = temp; //frame.ptr(0); // new uchar(numElem);
////    memcpy(m, frame.data, numElem * sizeof(uchar));

////    npy_intp mdim[] = { frame.rows, frame.cols, frame.channels()};
//    npy_intp mdim[] = {10,10,3};
//    PyObject *mat;
//    if (numChannels == 1)
//        mat = PyArray_SimpleNewFromData(2, mdim, NPY_UINT8, m);
//    else
//        mat = PyArray_SimpleNewFromData(3, mdim, NPY_UINT8, m);

//        ;

//    PyObject *pArgs; // = Py_BuildValue("(O)", mat);
//    PyObject *pName, *pModule, *pFunc;
//    PyObject *pValue;

//    pArgs = PyTuple_New (1);
//    PyTuple_SetItem (pArgs, 0, mat);

//    // Setup module and function(s)
//    pName = PyUnicode_DecodeFSDefault("pythonTest");
//    pModule = PyImport_Import(pName);
//    Py_DECREF(pName);
//    if (pModule != NULL) {

//        if (m_DLCInitInfDone == false) {
//            // Handles the first frame sent to DLC to init inference
//            pFunc = PyObject_GetAttrString(pModule, "initInference");
//            m_DLCInitInfDone = true;
//        }
//        else {
//            pFunc = PyObject_GetAttrString(pModule, "getPose");
//        }

//        if (pFunc && PyCallable_Check(pFunc)) {
////            qDebug() << "11111111";
//            pValue = PyObject_CallObject(pFunc, pArgs);
////            qDebug() << "2222222222";
//            if (pValue != NULL) {
//                qDebug() << "Result of call:"; //<< PyLong_AsLong(pValue);
//                Py_DECREF(pValue);
//            }
//            else {
//                Py_DECREF(pFunc);
//                Py_DECREF(pModule);
//                qDebug() << "Call failed";
//            }
//        }
//        else {
//            if (PyErr_Occurred())
//                PyErr_Print();
//            qDebug() << "Cannot find function";
//        }
//        Py_XDECREF(pFunc);
//        Py_DECREF(pModule);
//    }
//    else {
//        PyErr_Print();
//        qDebug() << "Failed to load";
//    }
//    Py_DECREF(mat);
//    Py_DECREF(pArgs);

#endif
    return returnPose;
}

void BehaviorTracker::startRunning()
{
    // Gets called when thread starts running

    // All the Python init stuff below needed to happen once the thread was running for some reason?!?!?!
#ifdef USE_PYTHON
    // TODO: Set parameters in user config file
    Py_SetPythonHome(L"C:/Users/dbaha/.conda/envs/dlc-live");
    Py_Initialize();

    // Adds .exe's directory to path to find py file
    PyObject* sysPath = PySys_GetObject((char*)"path");
    // TODO: Don't hard code this directory!
    PyObject* programName = PyUnicode_FromString("C:/Users/dbaha/Documents/Projects/Miniscope-DAQ-QT-Software/source/");
    PyList_Append(sysPath, programName);
    Py_DECREF(programName);
    Py_DECREF(sysPath);

    initNumpy(); // Inits import_array() and handles the return of it

#endif

    setUpDLCLive();
    // Will try using DLC's viewer before using our own

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
        QCoreApplication::processEvents(); // Is there a better way to do this. This is against best practices

    }

}
void BehaviorTracker::close()
{
    Py_Finalize();
//    view->close();
}
