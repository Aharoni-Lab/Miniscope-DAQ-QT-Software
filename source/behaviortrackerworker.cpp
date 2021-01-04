#include "behaviortrackerworker.h"

#include <QObject>
#include <QAtomicInt>
#include <QVector>
#include <QMap>
#include <QDebug>
#include <QString>
#include <QGuiApplication>
#include <QThread>
#include <QDir>

#ifdef USE_PYTHON
 #undef slots
 #include <Python.h>
 #include <numpy/arrayobject.h>
 #define slots
#endif


// ------ Pything MiniDLC Errors ------
#define ERROR_NONE                  0
#define ERROR_INIT                  9
#define ERROR_IMPORT                10
#define ERROR_INSTANCE              11
#define ERROR_SETUPDLC              12
#define ERROR_INIT_INF              13
#define ERROR_GET_POSE              14
#define ERROR_NUMPY_DLL             15

// ------------------------------------

BehaviorTrackerWorker::BehaviorTrackerWorker(QObject *parent, QJsonObject behavTrackerConfig):
    QObject(parent),
    numberOfCameras(0),
    m_PythonInitialized(false),
    m_PythonError(ERROR_NONE)
{
    m_btConfig = behavTrackerConfig;
}

void BehaviorTrackerWorker::initPython()
{
    // All the Python init stuff below needed to happen once the thread was running for some reason?!?!?!
#ifdef USE_PYTHON

    // Check to see if environment path goes to a likely python env with dlc
    if (QDir(m_btConfig["pyEnvPath"].toString() + "/Lib/site-packages/dlclive").exists()) {

        // Checks to make sure numpy .dll exists.
        if (QDir(m_btConfig["pyEnvPath"].toString() + "/Lib/site-packages/numpy/.libs").exists()) {

            // likely a correct path
            Py_SetPythonHome(m_btConfig["pyEnvPath"].toString().toStdWString().c_str());
            Py_Initialize();
            m_PythonInitialized = true;
            PyObject* sysPath = PySys_GetObject((char*)"path");
    //        qDebug() << "SYS PATH" << sysPath;
    //        QString pathToLib = m_btConfig["pyEnvPath"].toString() + "/Library/bin";
    //        PyList_Append(sysPath, PyUnicode_FromString(pathToLib.toStdString().c_str()));
            PyList_Append(sysPath, PyUnicode_FromString(".")); // appends path with current dir
            Py_DECREF(sysPath);
        }
        else {
            m_PythonError = ERROR_NUMPY_DLL;
            emit sendMessage("ERROR: Looks like your numpy install is missing a required DLL file. This happens if you use 'conda install numpy'. To fix, uninstall numpy from your environment and use 'pip install numpy' to reinstall it. <pyEnvPath>/Lib/site-packages/numpy/.libs should now exist.");
        }
    }
    else {
        // couldn't find dlclive in expected location. Possibly a bad path
        m_PythonError = ERROR_INIT;
        m_PythonInitialized = false;
        emit sendMessage("ERROR: Python not initialized. Check Python env path! Your path should contain: <pyEnvPath>/Lib/site-packages/dlclive.");
    }

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
//    pName = PyUnicode_DecodeFSDefault("Scripts.DLCwrapper");
    pModule = PyImport_Import(pName);
    Py_DECREF(pName);
    if (pModule == NULL) {
        m_PythonError = ERROR_IMPORT;
        emit sendMessage("ERROR: Cannot import './Scripts/DLCwrapper'. This usually is due to either a PATH issue or a problem importing all the modules in this .py file.");
    }
    else {
        pDict = PyModule_GetDict(pModule);
        pClass = PyDict_GetItemString(pDict, "MiniDLC");

        pArgs = PyTuple_New(2);
        pValue = PyUnicode_FromString(m_btConfig["modelPath"].toString().toUtf8());
        PyTuple_SetItem(pArgs, 0, pValue);

        pValue = PyFloat_FromDouble(m_btConfig["resize"].toDouble(1));
        PyTuple_SetItem(pArgs, 1, pValue);

        // Should create instance of MiniDLC class
        pInstance = PyObject_CallObject(pClass,pArgs);
        // If NULL something went wrong and we shouldn't do more with pInstance
        if (pInstance == NULL) {
            m_PythonError = ERROR_INSTANCE;
            emit sendMessage("ERROR: Behavior Tracker couldn't create instance of MiniDLC class from Scripts/DLCwrapper.py script.");
        }
        else {
            pValue = PyObject_CallMethod(pInstance,"setupDLC", NULL);
            if (PyFloat_AsDouble(pValue) != 0) {
                // Failure
                m_PythonError = ERROR_SETUPDLC;
                emit sendMessage("ERROR: 'setupDLC' failed in 'Scripts/DLCwrapper.py.");

            }
        }
    }

#endif
}

QVector<float> BehaviorTrackerWorker::getDLCLivePose(cv::Mat frame)
{

    QVector<float> pose;
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
        if (pValue == NULL) {
            m_PythonError = ERROR_INIT_INF;
            emit sendMessage("ERROR: Cannot init inference of DLC-Live in 'Scripts/DLCwrapper.py.");
        }
        else
            m_DLCInitInfDone = true;
    }
    else {
        pValue = PyObject_CallMethod(pInstance,"getPose", "(O)", mat);
        if (pValue == NULL) {
            m_PythonError = ERROR_GET_POSE;
            emit sendMessage("ERROR: Cannot get pose from DLC-Live in 'Scripts/DLCwrapper.py. Sometime you just need to restart the software.");
        }
        else {
            PyArrayObject *np_ret = reinterpret_cast<PyArrayObject*>(pValue);

            // Convert back to C++ array and print.

            npy_intp *arraySize = PyArray_SHAPE(np_ret);
            pose = QVector<float>(arraySize[0] * arraySize[1]);

            float *c_out;
            c_out = reinterpret_cast<float*>(PyArray_DATA(np_ret));

            for (int i = 0; i < arraySize[0] * arraySize[1]; i++){
                    pose[i] = c_out[i];
            }
        }
    }

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

void BehaviorTrackerWorker::setPoseBufferParameters(QVector<float> *poseBuf, int *poseFrameNumBuf, int poseBufSize, QAtomicInt *btPoseFrameNum, QSemaphore *free, QSemaphore *used)
{
    poseBuffer = poseBuf;
    poseFrameNumBuffer = poseFrameNumBuf;
    poseBufferSize = poseBufSize;
    m_btPoseCount = btPoseFrameNum;
    freePoses = free;
    usedPoses = used;

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
            colors[i*3+0] = (float)c_out[i*3]/170.0f;
            colors[i*3+1] = (float)c_out[i*3+1]/170.0f;
            colors[i*3+2] = (float)c_out[i*3+2]/170.0f;
     }
}

void BehaviorTrackerWorker::startRunning()
{
    // Gets called when thread starts running

    initPython();
    if (m_PythonError == ERROR_NONE) {
        initNumpy(); // Inits import_array() and handles the return of it

        setUpDLCLive();
        if (m_PythonError == ERROR_NONE) {
//            getColors(); // We don't used these colors anymore

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
                        if (m_PythonError != ERROR_NONE)
                            return;
                        poseFrameNumBuffer[idx % poseBufferSize] = (acqFrameNum - 1);
                        currentFrameNumberProcessed[camNames[camNum]] = acqFrameNum;
        //                qDebug() << acqFrameNum;
                        if (!freePoses->tryAcquire()) {
                            // Failed to acquire
                            // Pose will be thrown away
                            if (freePoses->available() == 0) {
                                emit sendMessage("Warning: Pose buffer full");
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
    }
}

void BehaviorTrackerWorker::close()
{
    m_trackingRunning = false;
#ifdef USE_PYTHON
    Py_Finalize();
#endif
}
