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
#include <QFile>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QVariant>
#include <QMetaType>

DataSaver::DataSaver(QObject *parent) :
    QObject(parent),
    baseDirectory(""),
    m_recording(false),
    behaviorTrackerEnabled(false),
    m_running(false)

{

}

bool DataSaver::setupFilePaths()
{
    QString tempString, tempString2;
    setupBaseDirectory();

    if (!QDir(baseDirectory).exists()) {
        if(!QDir().mkpath(baseDirectory)) {
            qDebug() << "Could not make path: " << baseDirectory;
            sendMessage("Error: Data folder structure failed to be created.");
            sendMessage("Error: Close program and correct User Config file.");
            return false;
            // TODO: When this happens stop recording
        }
        else {
            qDebug() << "Path to" << baseDirectory << "created.";
            sendMessage("Data being saved to : " + baseDirectory);
        }
    }
    else {
        qDebug() << baseDirectory << " already exisits. This likely will cause issues";
        sendMessage("Data being saved to : " + baseDirectory);
        sendMessage("Warning: " + baseDirectory + " already exisits. This likely will cause issues.");
    }

    // TODO: save metadata in base directory for experiment. Maybe some thing like saveBaseMetaDataJscon();

    // Setup directories for each recording device
    QJsonObject devices = m_userConfig["devices"].toObject();

    for (int i = 0; i < devices["miniscopes"].toArray().size(); i++) { // Miniscopes
        tempString = devices["miniscopes"].toArray()[i].toObject()["deviceName"].toString();
        tempString2 = tempString;
        tempString2.replace(" ", "_");
        deviceDirectory[tempString] = baseDirectory + "/" + tempString2;
        QDir().mkdir(deviceDirectory[tempString]);
    }
    for (int i = 0; i < devices["cameras"].toArray().size(); i++) { // Cameras
        tempString = devices["cameras"].toArray()[i].toObject()["deviceName"].toString();
        tempString2 = tempString;
        tempString2.replace(" ", "_");
        deviceDirectory[tempString] = baseDirectory + "/" + tempString2;
        QDir().mkdir(deviceDirectory[tempString]);
    }

    if (!m_userConfig["behaviorTracker"].toObject().isEmpty()) {
        QDir().mkdir(baseDirectory + "/behaviorTracker");
    }

    // Experiment Directory
    QDir().mkdir(baseDirectory + "/experiment");
    return true;
}

void DataSaver::setFrameBufferParameters(QString name,
                                         cv::Mat *frameBuf,
                                         qint64 *tsBuffer,
                                         float *bnoBuf,
                                         int bufSize,
                                         QSemaphore *freeFrames,
                                         QSemaphore *usedFrames,
                                         QAtomicInt *acqFrame)
{
    frameBuffer[name] = frameBuf;
    timeStampBuffer[name] = tsBuffer;
    bnoBuffer[name] = bnoBuf;
    bufferSize[name] = bufSize;
    freeCount[name] = freeFrames;
    usedCount[name] = usedFrames;

    acqFrameNum[name] = acqFrame;
    frameCount[name] = 0;
}

void DataSaver::setupBaseDirectory()
{
//    if (baseDirectory.isEmpty()) {
        QString tempString, tempString2;
        QJsonArray directoryStructure = m_userConfig["directoryStructure"].toArray();

        // Construct and make base directory
        baseDirectory = m_userConfig["dataDirectory"].toString();
        for (int i = 0; i < directoryStructure.size(); i++) {
            tempString = directoryStructure[i].toString();
            if (tempString == "date")
                baseDirectory += "/" + recordStartDateTime.date().toString("yyyy_MM_dd");
            else if (tempString == "time")
                baseDirectory += "/" + recordStartDateTime.time().toString("HH_mm_ss");
            else {
                tempString2 = m_userConfig[tempString].toString().replace(" ", "_");

                if (tempString2.isEmpty()) {
                    // Entry does not exist in User Config JSON file
                    sendMessage("Warning: " + tempString + " does not have associated value in User Config JSON file.");
                    sendMessage("Warning: Using /" + tempString + "Missing/ as place holder in data path.");
                    baseDirectory += "/" + tempString + "Missing";
                }
                else
                    baseDirectory += "/" + tempString2;
            }
//            else if (tempString == "researcherName")
//                baseDirectory += "/" + m_userConfig["researcherName"].toString().replace(" ", "_");
//            else if (tempString == "experimentName")
//                baseDirectory += "/" + m_userConfig["experimentName"].toString().replace(" ", "_");
//            else if (tempString == "animalName")
//                baseDirectory += " /" + m_userConfig["animalName"].toString().replace(" ", "_");
        }
//    }
}

void DataSaver::startRunning()
{
    if (m_running) {
        qCritical() << "Tried to run a DataSaver that was already running.";
        return;
    }

    m_running = true;
    int i, j;
    int bufPosition;
    int poseBufPosition;
    int fileNum;
    bool isColor;

    QString poseData;

    QString tempStr;
    QStringList names;
    while(m_running) {
        // For Behavior Tracker
        if (behaviorTrackerEnabled) {
            while (usedPoses->tryAcquire()) {
                if (m_recording) {
                    poseBufPosition = btPoseCount % poseBufferSize;
                    poseData.clear();
                    poseData.append(QString::number(poseFrameNumBuffer[poseBufPosition]) + ",");

                    for (j = 0; j < poseBuffer[poseBufPosition].length(); j++)
                        poseData.append(QString::number(poseBuffer[poseBufPosition][j], 'g', 3) + ",");
                    *behavTrackerStream << poseData << endl;
                }
                btPoseCount++;
                freePoses->release(1);
            }
        }

        // for video streams
        names = frameBuffer.keys();
        for (i = 0; i < frameBuffer.size(); i++) {
            while (usedCount[names[i]]->tryAcquire()) {
                // grad info from buffer in a threadsafe way
                if (m_recording) {
                    // save frame to file
                    if ((savedFrameCount[names[i]] % framesPerFile[names[i]]) == 0) {
                        // Create first as well as new video files
                        fileNum = (int) (savedFrameCount[names[i]] / framesPerFile[names[i]]);
                        tempStr = deviceDirectory[names[i]] + "/" + QString::number(fileNum) + ".avi";
                        videoWriter[names[i]]->release(); // release full file
                        if (frameBuffer[names[i]][0].channels() == 1)
                            isColor = false;
                        else
                            isColor = true;
                        // TODO: Add compression options here
                        if (ROI.contains(names[i])) {
                            // Need to trim frame to ROI
//                            qDebug() << ROI[names[i]][0] << ROI[names[i]][1] << ROI[names[i]][2] << ROI[names[i]][3];
                            videoWriter[names[i]]->open(tempStr.toUtf8().constData(),
                                    dataCompressionFourCC[names[i]], 60,
                                    cv::Size(ROI[names[i]][2], ROI[names[i]][3]), isColor); // color should be set to false?
                        }
                        else {
                            videoWriter[names[i]]->open(tempStr.toUtf8().constData(),
                                    dataCompressionFourCC[names[i]], 60,
                                    cv::Size(frameBuffer[names[i]][0].cols, frameBuffer[names[i]][0].rows), isColor); // color should be set to false?
                        }

                    }
                    bufPosition = frameCount[names[i]] % bufferSize[names[i]];
                    *csvStream[names[i]] << savedFrameCount[names[i]] << ","
                                         << (timeStampBuffer[names[i]][bufPosition] - recordStartDateTime.toMSecsSinceEpoch()) << ","
                                         << usedCount[names[i]]->available() << endl;

                    if (headOrientationStreamState[names[i]] == true && bnoBuffer[names[i]] != nullptr) {
                        if (headOrientationFilterState[names[i]] && bnoBuffer[names[i]][bufPosition*5 + 4] >= 0.05) { // norm is below 0.98. Should be 1 ideally
                            // Filter bad data and current data is bad
                        }
                        else {
                            *headOriStream[names[i]] << (timeStampBuffer[names[i]][bufPosition] - recordStartDateTime.toMSecsSinceEpoch()) << ","
                                                     << bnoBuffer[names[i]][bufPosition*5 + 0] << ","
                                                     << bnoBuffer[names[i]][bufPosition*5 + 1] << ","
                                                     << bnoBuffer[names[i]][bufPosition*5 + 2] << ","
                                                     << bnoBuffer[names[i]][bufPosition*5 + 3] << endl;
                        }

                    }

                    // TODO: Increment video file if reach max frame number per file
                    if (ROI.contains(names[i])) {
                        videoWriter[names[i]]->write(frameBuffer[names[i]][bufPosition](cv::Rect(ROI[names[i]][0],ROI[names[i]][1],ROI[names[i]][2],ROI[names[i]][3])));

                    }
                    else
                        videoWriter[names[i]]->write(frameBuffer[names[i]][bufPosition]);

                    savedFrameCount[names[i]]++;
                }

                frameCount[names[i]]++;
                freeCount[names[i]]->release(1);
            }
        }
        QCoreApplication::processEvents(); // Is there a better way to do this. This is against best practices
    }
}

void DataSaver::startRecording(QMap<QString,QVariant> ucInfo)
{

    // Update user config json object with any new info from control panel GUI
    QStringList keys = ucInfo.keys();
    for (int i=0; i < keys.size(); i++) {

//        if (ucInfo[keys[i]].canConvert(QMetaType::Int)) {
//            m_userConfig[keys[i]] = ucInfo[keys[i]].toInt();
//            qDebug() << keys[i] << ucInfo[keys[i]].toInt();
//        }
//        else {
            m_userConfig[keys[i]] = ucInfo[keys[i]].toString();
            qDebug() << keys[i] << ucInfo[keys[i]].toString();
//        }
    }

    if (m_recording) {
        // Data saver is already recording. This likely happens if there is a manual record and then an external trig record
        sendMessage("Warning: External 'START' trigger detected but software is already recording.");
        return;
    }
    QJsonDocument jDoc;
    recordStartDateTime = QDateTime::currentDateTime();
    if (setupFilePaths()) {
        // TODO: Save meta data JSONs
        jDoc = constructBaseDirectoryMetaData();
        saveJson(jDoc, baseDirectory + "/metaData.json");

        QString deviceName;
        // For Miniscopes
        for (int i = 0; i < m_userConfig["devices"].toObject()["miniscopes"].toArray().size(); i++) {
            deviceName = m_userConfig["devices"].toObject()["miniscopes"].toArray()[i].toObject()["deviceName"].toString();
            jDoc = constructDeviceMetaData("miniscopes",i);
            saveJson(jDoc, deviceDirectory[deviceName] + "/metaData.json");

            // Get user config frames per file
            framesPerFile[deviceName] = m_userConfig["devices"].toObject()["miniscopes"].toArray()[i].toObject()["framesPerFile"].toInt(1000);
        }
        // For Cameras
        for (int i = 0; i < m_userConfig["devices"].toObject()["cameras"].toArray().size(); i++) {
            deviceName = m_userConfig["devices"].toObject()["cameras"].toArray()[i].toObject()["deviceName"].toString();
            jDoc = constructDeviceMetaData("cameras", i);
            saveJson(jDoc, deviceDirectory[deviceName] + "/metaData.json");

            // Get user config frames per file
            framesPerFile[deviceName] = m_userConfig["devices"].toObject()["cameras"].toArray()[i].toObject()["framesPerFile"].toInt(1000);
        }

        // For Behavior Tracker
        if (!m_userConfig["behaviorTracker"].toObject().isEmpty()) {
            // TODO: Add behavior camera name(s) that are used in behaviorTracker
            jDoc.setObject(m_userConfig["behaviorTracker"].toObject());
            saveJson(jDoc, baseDirectory + "/behaviorTracker/metaData.json");

            // Create pose data file
            behavTrackerFile = new QFile(baseDirectory + "/behaviorTracker/pose.csv");
            behavTrackerFile->open(QFile::WriteOnly | QFile::Truncate);
            behavTrackerStream = new QTextStream(behavTrackerFile);

            // TODO: Add header info for behav tracker csv file here
            *behavTrackerStream << "DLC-Live Pose Data." << endl;
        }

        QString tempStr;
        QStringList keys = frameBuffer.keys();
        for (int i = 0; i < keys.length(); i++) {
            // loop through devices that make frames and setup csv file and videoWriter
            csvFile[keys[i]] = new QFile(deviceDirectory[keys[i]] + "/timeStamps.csv");
            csvFile[keys[i]]->open(QFile::WriteOnly | QFile::Truncate);
            csvStream[keys[i]] = new QTextStream(csvFile[keys[i]]);
            *csvStream[keys[i]] << "Frame Number,Time Stamp (ms),Buffer Index" << endl;

            if (headOrientationStreamState[keys[i]] == true && bnoBuffer[keys[i]] != nullptr) {
                headOriFile[keys[i]] = new QFile(deviceDirectory[keys[i]] + "/headOrientation.csv");
                headOriFile[keys[i]]->open(QFile::WriteOnly | QFile::Truncate);
                headOriStream[keys[i]] = new QTextStream(headOriFile[keys[i]]);
                *headOriStream[keys[i]] << "Time Stamp (ms),qw,qx,qy,qz" << endl;
            }
            // TODO: Remember to close files on exit or stop recording signal

            videoWriter[keys[i]] = new cv::VideoWriter();
            // TODO: Correctly enter size of videoWriter
    //         TODO: Release videoWriters at exit

            savedFrameCount[keys[i]] = 0;


        }

        // Creates note csv file
        noteFile = new QFile(baseDirectory + "/notes.csv");
        noteFile->open(QFile::WriteOnly | QFile::Truncate);
        noteStream = new QTextStream(noteFile);

        // TODO: Save camera calibration file to data directory for each behavioral camera
        m_recording = true;
    }
    else {
        qDebug() << "Failed to record due to base directory creation failing";
        // TODO: Stop counting and disable buttons since data directory needs to be corrected in user config
    }
}

void DataSaver::stopRecording()
{
    if (!m_recording) {
        sendMessage("Warning: External 'STOP' trigger detected but software is not recording.");
        return;
    }
    m_recording = false;
    QStringList keys = videoWriter.keys();
    for (int i = 0; i < keys.length(); i++) {
        videoWriter[keys[i]]->release();
        csvFile[keys[i]]->close();

        if (headOrientationStreamState[keys[i]] == true && bnoBuffer[keys[i]] != nullptr)
            if (headOriFile[keys[i]]->isOpen())
                headOriFile[keys[i]]->close();
    }
    if (!m_userConfig["behaviorTracker"].toObject().isEmpty()) {
        if (behavTrackerFile->isOpen())
            behavTrackerFile->close();
    }
    noteFile->close();
}

void DataSaver::devicePropertyChanged(QString deviceName, QString propName, QVariant propValue)
{
    deviceProperties[deviceName][propName] = propValue;
    qDebug() << deviceName << propName << propValue;
    // TODO: signal change to filing keeping track of changes during recording
}

void DataSaver::takeScreenShot(QString type)
{
    QDateTime dtTemp = QDateTime().currentDateTime();

    QString filename = type + "_" + dtTemp.toString("yyyy_MM_dd_HH_mm_ss") + ".png";

    if (baseDirectory.isEmpty())
        setupBaseDirectory();
    QString fullFilePath = baseDirectory;
    fullFilePath.replace("//","/");
    fullFilePath.replace("//","/");
    if (fullFilePath.right(1) == "/")
        fullFilePath.chop(1);
    fullFilePath += "/imageCaptures";
    if (!QDir(fullFilePath).exists()){

        qDebug() << QDir().mkpath(fullFilePath);
    }

    fullFilePath += "/" + filename;
    int idx = (*acqFrameNum[type] -1) % bufferSize[type];
//    qDebug() << "Index = " << idx;
    sendMessage("Taking screenshot of " + type + ".");

    cv::imwrite(fullFilePath.toUtf8().constData(), frameBuffer[type][idx] );
}

void DataSaver::takeNote(QString note)
{
    // Writes note to file submitted through control panel
    // Only write notes when recording
    if (m_recording) {
        *noteStream << QDateTime().currentMSecsSinceEpoch() - recordStartDateTime.toMSecsSinceEpoch() << "," << note << endl;
    }
}

void DataSaver::setDataCompression(QString name, QString type)
{
    dataCompressionFourCC[name] =cv::VideoWriter::fourcc(type.toStdString()[0],type.toStdString()[1],type.toStdString()[2],type.toStdString()[3]);


    qDebug() << name << type << dataCompressionFourCC[name];

}

void DataSaver::setROI(QString name, int *bbox)
{
    ROI[name] = bbox;
}

void DataSaver::setPoseBufferParameters(QVector<float> *poseBuf, int *poseFrameNumBuf, int poseBufSize, QSemaphore *freePos, QSemaphore *usedPos)
{
    poseBuffer = poseBuf;
    poseFrameNumBuffer = poseFrameNumBuf;
    poseBufferSize = poseBufSize;
    freePoses = freePos;
    usedPoses = usedPos;
    btPoseCount = 0;
    behaviorTrackerEnabled = true;
}

QJsonDocument DataSaver::constructBaseDirectoryMetaData()
{
    QJsonObject metaData;
    QJsonDocument jDoc;
    QString tempString;

    QJsonArray directoryStructure = m_userConfig["directoryStructure"].toArray();
    for (int i = 0; i < directoryStructure.size(); i++) {
        tempString = directoryStructure[i].toString();
        if (tempString != "date" && tempString != "time" && !tempString.isEmpty())
            metaData[tempString] = m_userConfig[tempString].toString();
    }
    if (!metaData.contains("researcherName"))
        metaData["researcherName"] = m_userConfig["researcherName"].toString();
    if (!metaData.contains("animalName"))
        metaData["animalName"] = m_userConfig["animalName"].toString();
    if (!metaData.contains("experimentName"))
        metaData["experimentName"] = m_userConfig["experimentName"].toString();

    metaData["baseDirectory"] = baseDirectory;

    // Start time
    metaData["year"] = recordStartDateTime.date().year();
    metaData["month"] = recordStartDateTime.date().month();
    metaData["day"] = recordStartDateTime.date().day();
    metaData["hour"] = recordStartDateTime.time().hour();
    metaData["minute"] = recordStartDateTime.time().minute();
    metaData["second"] = recordStartDateTime.time().second();
    metaData["msec"] = recordStartDateTime.time().msec();
    metaData["msecSinceEpoch"] = recordStartDateTime.toMSecsSinceEpoch();

    //Device info
    QStringList list;
    for (int i = 0; i < m_userConfig["devices"].toObject()["miniscopes"].toArray().size(); i++)
        list.append(m_userConfig["devices"].toObject()["miniscopes"].toArray()[i].toObject()["deviceName"].toString());
    metaData["miniscopes"] = QJsonArray().fromStringList(list);

    list.clear();
    for (int i = 0; i < m_userConfig["devices"].toObject()["cameras"].toArray().size(); i++)
        list.append(m_userConfig["devices"].toObject()["cameras"].toArray()[i].toObject()["deviceName"].toString());
    metaData["cameras"] = QJsonArray().fromStringList(list);

    list.clear();
    if (!m_userConfig["behaviorTracker"].toObject().isEmpty())
        metaData["behaviorTracker"] = m_userConfig["behaviorTracker"].toObject();

    jDoc.setObject(metaData);
    return jDoc;
}

QJsonDocument DataSaver::constructDeviceMetaData(QString type, int idx)
{
    QJsonObject metaData;
    QJsonDocument jDoc;

    QJsonObject deviceObj = m_userConfig["devices"].toObject()[type].toArray()[idx].toObject();
    QString deviceName = deviceObj["deviceName"].toString();

    metaData["deviceName"] = deviceName;
    metaData["deviceType"] = deviceObj["deviceType"].toString();
    metaData["deviceID"] = deviceObj["deviceID"].toInt();
    metaData["deviceDirectory"] = deviceDirectory[deviceName];
    metaData["framesPerFile"] = deviceObj["framesPerFile"].toInt(1000);
    metaData["compression"] = deviceObj["compression"].toString("FFV1");

    if (ROI.contains(deviceName)) {
        QJsonObject jROI;
        jROI["leftEdge"] = ROI[deviceName][0];
        jROI["topEdge"] = ROI[deviceName][1];
        jROI["width"] = ROI[deviceName][2];
        jROI["height"] = ROI[deviceName][3];
        metaData["ROI"] = jROI;
    }
    // loop through device properties at the start of recording
    QStringList keys = deviceProperties[deviceName].keys();
    for (int i = 0; i < keys.length(); i++) {
        if (deviceProperties[deviceName][keys[i]].userType() == QMetaType::QString)
            metaData[keys[i]] = deviceProperties[deviceName][keys[i]].toString();
        else
            metaData[keys[i]] = deviceProperties[deviceName][keys[i]].toDouble();
    }

    jDoc.setObject(metaData);
    return jDoc;
}

void DataSaver::saveJson(QJsonDocument document, QString fileName)
{
    QFile jsonFile(fileName);
    jsonFile.open(QFile::NewOnly);
    jsonFile.write(document.toJson());

}

