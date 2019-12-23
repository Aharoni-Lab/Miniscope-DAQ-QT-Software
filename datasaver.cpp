#include "datasaver.h"

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDir>
#include <QDebug>

DataSaver::DataSaver(QObject *parent) :
    QObject(parent),
    m_recording(false),
    m_running(false)
{

}

void DataSaver::setupFilePaths()
{
    QString tempString;
    QJsonArray directoryStructure = m_userConfig["directoryStructure"].toArray();

    // Construct and make base directory
    baseDirectory = m_userConfig["dataDirectory"].toString();
    for (int i = 0; i < directoryStructure.size(); i++) {
        tempString = directoryStructure[i].toString();
        if (tempString == "date")
            baseDirectory += "/" + recordStartDateTime.date().toString("yyyy_MM_dd");
        else if (tempString == "time")
            baseDirectory += "/" + recordStartDateTime.time().toString("HH_mm_ss");
        else if (tempString == "researcherName")
            baseDirectory += "/" + m_userConfig["researcherName"].toString().replace(" ", "_");
        else if (tempString == "experimentName")
            baseDirectory += "/" + m_userConfig["experimentName"].toString().replace(" ", "_");
        else if (tempString == "animalName")
            baseDirectory += "/" + m_userConfig["animalName"].toString().replace(" ", "_");
    }

    if (!QDir(baseDirectory).exists())
        if(!QDir().mkpath(baseDirectory))
            qDebug() << "Could not make path: " << baseDirectory;

    // TODO: save metadata in base directory for experiment. Maybe some thing like saveBaseMetaDataJscon();

    // Setup directories for each recording device
    QJsonObject devices = m_userConfig["devices"].toObject();

    for (int i = 0; i < devices["miniscopes"].toArray().size(); i++) { // Miniscopes
        tempString = devices["miniscopes"].toArray()[i].toObject()["deviceName"].toString();
        deviceDirectory[tempString] = baseDirectory + "/" + tempString.replace(" ", "_");
        QDir().mkdir(deviceDirectory[tempString]);
    }
    for (int i = 0; i < devices["cameras"].toArray().size(); i++) { // Cameras
        tempString = devices["miniscopes"].toArray()[i].toObject()["deviceName"].toString();
        deviceDirectory[tempString] = baseDirectory + "/" + tempString.replace(" ", "_");
        QDir().mkdir(deviceDirectory[tempString]);
    }
    // Experiment Directory
    QDir().mkdir(baseDirectory + "/experiment");
}

void DataSaver::setFrameBufferParameters(QString name,
                                         cv::Mat *frameBuf,
                                         qint64 *tsBuffer,
                                         QSemaphore *freeFrames,
                                         QSemaphore *usedFrames)
{
    frameBuffer[name] = frameBuf;
    timeStampBuffer[name] = tsBuffer;
    freeCount[name] = freeFrames;
    usedCount[name] = usedFrames;

    frameCount[name] = 0;
}

void DataSaver::startRunning()
{
    m_running = true;
    int i;
    QStringList names;
    while(m_running) {
        // for video streams
        names = frameBuffer.keys();
        for (i = 0; i < frameBuffer.size(); i++) {
            while (usedCount[names[i]]->tryAcquire()) {
                // grad info from buffer in a threadsafe way
                if (m_recording) {
                    // save frame to file
                }

                frameCount[names[i]]++;
                freeCount[names[i]]->release(1);
            }
        }
    }
}

void DataSaver::startRecording()
{
    recordStartDateTime = QDateTime::currentDateTime();
    setupFilePaths();
    m_recording = true;
}

