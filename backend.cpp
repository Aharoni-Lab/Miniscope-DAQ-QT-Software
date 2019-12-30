#include "backend.h"
#include <QDebug>
#include <QFileDialog>
#include <QApplication>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QObject>

#include "miniscope.h"
#include "behaviorcam.h"
#include "controlpanel.h"
#include "datasaver.h"

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

    dataSaver = new DataSaver();
}

void backEnd::setUserConfigFileName(const QString &input)
{
    if (input != m_userConfigFileName) {
        m_userConfigFileName = input;
        //emit userConfigFileNameChanged();
    }

    loadUserConfigFile();
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

        setupDataSaver(); // must happen after devices have been made
    }
    else {
        // TODO: throw out error
    }

}

void backEnd::onRecordClicked()
{
    //TODO: tell dataSaver to start recording

    // TODO: start experiment running
}

void backEnd::connectSnS()
{

    // Start and stop recording signals
    QObject::connect(controlPanel, SIGNAL( recordStart()), dataSaver, SLOT (startRecording()));
    QObject::connect(controlPanel, SIGNAL( recordStop()), dataSaver, SLOT (stopRecording()));

    QObject::connect(dataSaver, SIGNAL(sendMessage(QString)), controlPanel, SLOT( receiveMessage(QString)));

    for (int i = 0; i < miniscope.length(); i++) {
        // Connect send and receive message to textbox in controlPanel
        QObject::connect(miniscope[i], SIGNAL(sendMessage(QString)), controlPanel, SLOT( receiveMessage(QString)));

        // For triggering screenshots
        QObject::connect(miniscope[i], SIGNAL(takeScreenShot(QString)), dataSaver, SLOT( takeScreenShot(QString)));
    }
    for (int i = 0; i < behavCam.length(); i++) {
        QObject::connect(behavCam[i], SIGNAL(sendMessage(QString)), controlPanel, SLOT( receiveMessage(QString)));
    }




}

void backEnd::setupDataSaver()
{
    dataSaver->setUserConfig(m_userConfig);
    dataSaver->setRecord(false);
//    dataSaver->startRecording();

    for (int i = 0; i < miniscope.length(); i++) {
        dataSaver->setFrameBufferParameters(miniscope[i]->getDeviceName(),
                                            miniscope[i]->getFrameBufferPointer(),
                                            miniscope[i]->getTimeStampBufferPointer(),
                                            miniscope[i]->getBufferSize(),
                                            miniscope[i]->getFreeFramesPointer(),
                                            miniscope[i]->getUsedFramesPointer(),
                                            miniscope[i]->getAcqFrameNumPointer());
    }
    // TODO: setup buffer and thread safe connections for cameras

    dataSaverThread = new QThread;
    dataSaver->moveToThread(dataSaverThread);

    QObject::connect(dataSaverThread, SIGNAL (started()), dataSaver, SLOT (startRunning()));
    // TODO: setup start connections

    dataSaverThread->start();
}

bool backEnd::checkUserConfigForIssues()
{
    // TODO: check user config for issues
    return true;
}

void backEnd::parseUserConfig()
{
    QJsonObject devices = m_userConfig["devices"].toObject();

    // Main JSON header
    researcherName = m_userConfig["researcherName"].toString();
    dataDirectory= m_userConfig["dataDirectory"].toString();
    dataStructureOrder = m_userConfig["dataStructureOrder"].toArray();
    experimentName = m_userConfig["experimentName"].toString();
    animalName = m_userConfig["animalName"].toString();

    // JSON subsections
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
        QObject::connect(miniscope.last(),
                         SIGNAL (onPropertyChanged(QString, QString, double)),
                         dataSaver,
                         SLOT (devicePropertyChanged(QString, QString, double)));
        miniscope.last()->createView();
    }
    for (idx = 0; idx < ucBehaviorCams.size(); idx++) {
        // un comment below once behav cam class is written
        //behavCam.append(new BehaviorCam(this, ucBehaviorCams[idx].toObject()));
    }
    if (!ucExperiment.isEmpty()){
        // Construct experiment interface
    }
    if (!ucBehaviorTracker.isEmpty()) {
        // Setup behavior tracker
    }
    // Load main control GUI
    controlPanel = new ControlPanel(this, m_userConfig);

    connectSnS();
}
