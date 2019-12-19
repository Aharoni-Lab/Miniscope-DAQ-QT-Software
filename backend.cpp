#include "backend.h"
#include <QDebug>
#include <QFileDialog>
#include <QApplication>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "miniscope.h"

#define DEBUG

backEnd::backEnd(QObject *parent) :
    QObject(parent),
    m_userConfigOK(false)
{
#ifdef DEBUG
    m_userConfigFileName = ":/userConfigs/UserConfigExample.json";
    loadUserConfigFile();
    setUserConfigOK(true);
#endif

//    m_userConfigOK = false;

    // User Config default values
    researcherName = "";
    dataDirectory = "";
    experimentName = "";
    animalName = "";
    dataStructureOrder = {"researcherName", "experimentName", "animalName", "date"};

    ucExperiment["type"] = "None";
    ucMiniscopes = {"None"};
    ucBehaviorCams = {"None"};
    ucBehaviorTracker["type"] = "None";
}

QString backEnd::userConfigFileName()
{
    return m_userConfigFileName;
}

void backEnd::setUserConfigFileName(const QString &input)
{
    if (input != m_userConfigFileName) {
        m_userConfigFileName = input;
        //emit userConfigFileNameChanged();
    }

    loadUserConfigFile();
}

QString backEnd::userConfigDisplay()
{
    return m_userConfigDisplay;
}

void backEnd::setUserConfigDisplay(const QString &input)
{
    if (input != m_userConfigDisplay) {
        m_userConfigDisplay = input;
        emit userConfigDisplayChanged();
    }
}

void backEnd::loadUserConfigFile()
{
    QString jsonFile;
    QFile file;
    file.setFileName(m_userConfigFileName);
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    jsonFile = file.readAll();
    setUserConfigDisplay("User Config File Selected: " + m_userConfigFileName + "\n" + jsonFile);
    file.close();
    QJsonDocument d = QJsonDocument::fromJson(jsonFile.toUtf8());
    m_userConfig = d.object();
}

void backEnd::onRunClicked()
{
//    qDebug() << "Run was clicked!";
    m_userConfigOK = checkUserConfigForIssues();
    if (m_userConfigOK) {
        parseUserConfig();
        constructUserConfigGUI();
    }

}

bool backEnd::checkUserConfigForIssues()
{
    // TODO: check user config for issues
    return true;
}

void backEnd::parseUserConfig()
{
    QJsonObject devices = m_userConfig["devices"].toObject();

    researcherName = m_userConfig["researcherName"].toString();
    dataDirectory= m_userConfig["dataDirectory"].toString();
    dataStructureOrder = m_userConfig["dataStructureOrder"].toArray();
    experimentName = m_userConfig["experimentName"].toString();
    animalName = m_userConfig["animalName"].toString();

    qDebug() << researcherName << " " << dataDirectory << " " << experimentName << " " << animalName;

    ucExperiment = m_userConfig["experiment"].toObject();
    ucMiniscopes = devices["miniscopes"].toArray();
    ucBehaviorCams = devices["cameras"].toArray();
    ucBehaviorTracker = m_userConfig["behaviorTracking"].toObject();
}

void backEnd::constructUserConfigGUI()
{
    int idx;
    for (idx = 0; idx < ucMiniscopes.size(); idx++) {
        miniscope.append(new Miniscope(this, ucMiniscopes[idx].toObject()));
    }
    if (ucBehaviorCams.size() > 0) {
        // Construct camera GUI(s)
    }
    if (!ucExperiment.isEmpty()){
        // Construct experiment interface
    }
    if (!ucBehaviorTracker.isEmpty()) {
        // Setup behavior tracker
    }
}
