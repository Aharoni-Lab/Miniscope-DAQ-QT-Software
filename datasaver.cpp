#include "datasaver.h"

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDir>
#include <QDebug>

DataSaver::DataSaver(QObject *parent) : QObject(parent)
{
}

void DataSaver::setupFilePaths()
{
    QString tempString;
    QJsonArray directoryStructure = m_userConfig["directoryStructure"].toArray();
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
    qDebug() << baseDirectory;
    if (!QDir(baseDirectory).exists())
        if(!QDir().mkpath(baseDirectory))
            qDebug() << "Could not make path: " << baseDirectory;



}

void DataSaver::startRecording()
{
    recordStartDateTime = QDateTime::currentDateTime();
    setupFilePaths();
}
