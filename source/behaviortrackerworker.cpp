#include "behaviortrackerworker.h"

#include <QObject>
#include <QAtomicInt>
#include <QVector>
#include <QMap>
#include <QDebug>
#include <QString>
#include <QGuiApplication>
#include <QThread>

#ifdef USE_PYTHON
 #undef slots
 #include <Python.h>
 #include <numpy/arrayobject.h>
 #define slots
#endif

BehaviorTrackerWorker::BehaviorTrackerWorker(QObject *parent, QJsonObject behavTrackerConfig):
    QObject(parent),
    numberOfCameras(0)
{
    m_btConfig = behavTrackerConfig;
}

void BehaviorTrackerWorker::initPython()
{
    // All the Python init stuff below needed to happen once the thread was running for some reason?!?!?!
#ifdef USE_PYTHON
    // TODO: Set parameters in user config file
    Py_SetPythonHome(m_btConfig["pyEnvPath"].toString().toStdWString().c_str());
    Py_Initialize();

    // Adds .exe's directory to path to find py file
    PyObject* sysPath = PySys_GetObject((char*)"path");
    PyList_Append(sysPath, PyUnicode_FromString(".")); // appends path with current dir
    // TODO: Don't hard code this directory!
//    PyObject* programName = PyUnicode_FromString("C:/Users/dbaha/Documents/Projects/Miniscope-DAQ-QT-Software/source/");
//    PyList_Append(sysPath, programName);
//    Py_DECREF(programName);
    Py_DECREF(sysPath);
#endif
}

int BehaviorTrackerWorker::initNumpy()
{
    import_array1(-1);
}

void BehaviorTrackerWorker::setUpDLCLive()
{
#ifdef USE_PYTHON
    PyObject *pName;

    pName = PyUnicode_DecodeFSDefault("Scripts.DLCwrapper");
    pModule = PyImport_Import(pName);
    Py_DECREF(pName);
    pDict = PyModule_GetDict(pModule);

    pClass = PyDict_GetItemString(pDict, "MiniDLC");

    pArgs = PyTuple_New(2);
    pValue = PyUnicode_FromString(m_btConfig["modelPath"].toString().toUtf8());
    PyTuple_SetItem(pArgs, 0, pValue);

    pValue = PyFloat_FromDouble(m_btConfig["resize"].toDouble(1));
    PyTuple_SetItem(pArgs, 1, pValue);

    pInstance = PyObject_CallObject(pClass,pArgs);

//    pValue = PyObject_CallMethod(pInstance,"initInference",NULL);

//    Py_DECREF(pValue);
//    Py_DECREF(pArgs);
//    Py_DECREF(pClass);
//    Py_DECREF(pDict);
//    Py_DECREF(pModule);
#endif
}

QVector<float> BehaviorTrackerWorker::getDLCLivePose(cv::Mat frame)
{

#ifdef USE_PYTHON
    PyObject *mat;

    uchar* m = frame.ptr(0);
    npy_intp mdim[] = { frame.rows, frame.cols, frame.channels()};

    if (frame.channels() == 1)
        mat = PyArray_SimpleNewFromData(2, mdim, NPY_UINT8, m);
    else
        mat = PyArray_SimpleNewFromData(3, mdim, NPY_UINT8, m);

    if (m_DLCInitInfDone == false) {
        pValue = PyObject_CallMethod(pInstance,"initInference", "(O)", mat);
        m_DLCInitInfDone = true;
    }
    else
        pValue = PyObject_CallMethod(pInstance,"getPose", "(O)", mat);

    PyArrayObject *np_ret = reinterpret_cast<PyArrayObject*>(pValue);

    // Convert back to C++ array and print.

    npy_intp *arraySize = PyArray_SHAPE(np_ret);
    QVector<float> pose(arraySize[0] * arraySize[1]);

    float *c_out;
    c_out = reinterpret_cast<float*>(PyArray_DATA(np_ret));

    for (int i = 0; i < arraySize[0] * arraySize[1]; i++){
            pose[i] = c_out[i];
    }
//    qDebug() << pose;
//    QThread::msleep(50);



//    Py_DECREF(pValue);
    Py_DECREF(mat);
//    Py_DECREF(pArgs);

#endif
    return pose;
}

void BehaviorTrackerWorker::setParameters(QString name, cv::Mat *frameBuf, int bufSize, QAtomicInt *acqFrameNum)
{
    frameBuffer[name] = frameBuf;
    bufferSize[name] = bufSize;
    m_acqFrameNum[name] = acqFrameNum;

    currentFrameNumberProcessed[name] = 0;
    numberOfCameras++;
}

void BehaviorTrackerWorker::setPoseBufferParameters(QVector<float> *poseBuf, int *poseFrameNumBuf, int poseBufSize, QAtomicInt *btPoseFrameNum, QSemaphore *free, QSemaphore *used, uint8_t *pColors)
{
    poseBuffer = poseBuf;
    poseFrameNumBuffer = poseFrameNumBuf;
    poseBufferSize = poseBufSize;
    m_btPoseCount = btPoseFrameNum;
    freePoses = free;
    usedPoses = used;

    colors = pColors;
}

void BehaviorTrackerWorker::getColors()
{
    // TODO: Currently number of colors is hardcoded. Change this
    pValue = PyObject_CallMethod(pInstance,"getColors", NULL);

    PyArrayObject *np_ret = reinterpret_cast<PyArrayObject*>(pValue);

    // Convert back to C++ array and print.

//    npy_intp *arraySize = PyArray_SHAPE(np_ret);
//    uint8_t colors[20][3];

    uint8_t *c_out;
    c_out = reinterpret_cast<uint8_t*>(PyArray_DATA(np_ret));

    for (int i = 0; i < 20; i++){
            colors[i*3] = c_out[i*3];
            colors[i*3+1] = c_out[i*3+1];
            colors[i*3+2] = c_out[i*3+2];
     }
}

void BehaviorTrackerWorker::startRunning()
{
    // Gets called when thread starts running

    initPython();

    initNumpy(); // Inits import_array() and handles the return of it

    setUpDLCLive();

    getColors();
    // Will try using DLC's viewer before using our own

    m_trackingRunning = true;
    int acqFrameNum;
    int frameIdx;
    int idx = 0;
    QList<QString> camNames = frameBuffer.keys();
    while (m_trackingRunning) {
        for (int camNum = 0; camNum < camNames.length(); camNum++) {
            // Loops through cameras to see if new frames are ready
            acqFrameNum = *m_acqFrameNum[camNames[camNum]];
            if (acqFrameNum > currentFrameNumberProcessed[camNames[camNum]]) {
                // New frame ready for behavior tracking
                frameIdx = (acqFrameNum - 1) % bufferSize[camNames[camNum]];
                poseBuffer[idx % poseBufferSize] = getDLCLivePose(frameBuffer[camNames[camNum]][frameIdx]);
                poseFrameNumBuffer[idx % poseBufferSize] = (acqFrameNum - 1);
                currentFrameNumberProcessed[camNames[camNum]] = acqFrameNum;
//                qDebug() << acqFrameNum;
                if (!freePoses->tryAcquire()) {
                    // Failed to acquire
                    // Pose will be thrown away
                    if (freePoses->available() == 0) {
                        sendMessage("Warning: Pose buffer full");
                        QThread::msleep(500);
                    }
                }
                else {
                    idx++;
                    m_btPoseCount->operator++();
                    usedPoses->release();
                }
            }
        }
        QCoreApplication::processEvents(); // Is there a better way to do this. This is against best practices
    }

}

void BehaviorTrackerWorker::close()
{
    m_trackingRunning = false;
#ifdef USE_PYTHON
    Py_Finalize();
#endif
}
