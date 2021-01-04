#include "controlpanel.h"


#include "newquickview.h"


#include <QObject>
#include <QGuiApplication>
#include <QScreen>
#include <QQuickItem>
#include <QJsonObject>
#include <QString>
#include <QTimer>
#include <QTime>
#include <QMetaObject>
#include <QProcess>
#include <QJsonArray>
#include <QMap>

ControlPanel::ControlPanel(QObject *parent, QJsonObject userConfig) :
    QObject(parent),
    currentRecordTime(0),
    m_recording(false)
{
    m_userConfig = userConfig;

    recordTimer = new QTimer(this);
    QObject::connect(recordTimer, &QTimer::timeout, this, &ControlPanel::recordTimerTick);

    createView();
}

void ControlPanel::createView()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect  screenGeometry = screen->geometry();
    int height = screenGeometry.height();
    int width = screenGeometry.width();

    const QUrl url(QStringLiteral("qrc:/ControlPanel.qml"));
    view = new NewQuickView(url);

    qDebug() << "Screen size is " << width << " x " << height;
    view->setWidth(400);
    view->setHeight(height*0.9);
    view->setTitle("Control Panel");
    view->setX(1);
    view->setY(50);

#ifdef Q_OS_WINDOWS
        view->setFlags(Qt::Window | Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint);
#endif
    view->show();

    rootObject = view->rootObject();
    messageTextArea = rootObject->findChild<QQuickItem*>("messageTextArea");

    recordTimeText = rootObject->findChild<QQuickItem*>("recordTimeText");

    m_ucRecordLengthinSeconds = m_userConfig["recordLengthinSeconds"].toInt(0);
    rootObject->setProperty("currentRecordTime", 0);
    rootObject->setProperty("ucRecordLength", m_ucRecordLengthinSeconds);
//    recordTimeText->setProperty("text", "----/" + QString::number(m_ucRecordLengthinSeconds) + " s");

    fillUCEditText();
    connectSnS();
}

void ControlPanel::connectSnS()
{

    QObject::connect(rootObject->findChild<QQuickItem*>("bRecord"),
                     SIGNAL(activated()),
                     this,
                     SLOT(onRecordActivated()));
    QObject::connect(rootObject->findChild<QQuickItem*>("bStop"),
                     SIGNAL(activated()),
                     this,
                     SLOT(onStopActivated()));
    QObject::connect(rootObject,
                     SIGNAL(submitNoteSignal(QString)),
                     this,
                     SLOT(handleNoteSumbit(QString)));
    QObject::connect(rootObject,
                     SIGNAL(extTriggerSwitchToggled(bool)),
                     this,
                     SLOT(extTriggerSwitchToggled2(bool)));


}

void ControlPanel::fillUCEditText()
{
    QList<QVariant> props, values, isNumber;
    QStringList keys = m_userConfig.keys();
    for (int i=0; i< keys.size(); i++) {
        if (m_userConfig[keys[i]].isString()) {
            props.append(keys[i]);
            values.append(m_userConfig[keys[i]].toString());
            isNumber.append(0);
        }
        if (m_userConfig[keys[i]].isDouble()) {
            props.append(keys[i]);
            values.append(m_userConfig[keys[i]].toDouble());
            isNumber.append(1);
        }
    }
    rootObject->setProperty("ucProps", props);
    rootObject->setProperty("ucValues", values);
    rootObject->setProperty("ucIsNumber", isNumber);
}

void ControlPanel::receiveMessage(QString msg)
{
    // Add msg to textbox in controlPanel.qml
    QTime time;
    QMetaObject::invokeMethod(messageTextArea, "logMessage",
        Q_ARG(QVariant, time.currentTime().toString("HH:mm:ss")),
        Q_ARG(QVariant, msg));
}

void ControlPanel::onRecordActivated()
{
    QJsonObject exeInfo;
    QStringList argList;
    QJsonArray argArray;
    if (m_userConfig.contains("executableOnStartRecording")) {
        // Lets setup a exe to execute
        exeInfo = m_userConfig["executableOnStartRecording"].toObject();
        if (exeInfo["enabled"].toBool(true)) {
            argArray = m_userConfig["arguments"].toArray();
            for (int i=0; i < argArray.size(); i++) {
                argList.append(argArray[i].toString());
            }
            QProcess::startDetached(exeInfo["filePath"].toString(""), argList);
        }
    }

    // Construct uc props that might have been changed by user
    QStringList ucPropsList = rootObject->property("ucProps").toStringList();
    QList<QVariant> ucValuesList = rootObject->property("ucValues").toList();
    QMap<QString,QVariant> ucInfo;
    for (int i=0; i< ucPropsList.size(); i++) {
        ucInfo[ucPropsList[i]] = ucValuesList[i];

        // Updates the record time in the tick timer inturrupt function
        if (ucPropsList[i] == "recordLengthinSeconds")
            m_ucRecordLengthinSeconds = ucValuesList[i].toInt();
    }
    recordStart(ucInfo);

    m_recording = true;
    rootObject->setProperty("recording", true);
    currentRecordTime = 0;
    recordTimer->start(1000);
    receiveMessage("Recording Started.");
}

void ControlPanel::onStopActivated()
{
    QJsonObject exeInfo;
    QStringList argList;
    QJsonArray argArray;
    if (m_userConfig.contains("executableOnStopRecording")) {
        // Lets setup a exe to execute
        exeInfo = m_userConfig["executableOnStopRecording"].toObject();
        if (exeInfo["enabled"].toBool(true)) {
            argArray = m_userConfig["arguments"].toArray();
            for (int i=0; i < argArray.size(); i++) {
                argList.append(argArray[i].toString());
            }
            QProcess::startDetached(exeInfo["filePath"].toString(""), argList);
        }
    }

    recordStop();
    receiveMessage("Recording Stopped.");
    m_recording = false;
    rootObject->setProperty("recording", false);
    if (recordTimer->isActive())
        recordTimer->stop();

}

void ControlPanel::recordTimerTick()
{
    currentRecordTime++;
    rootObject->setProperty("currentRecordTime", currentRecordTime);
    if (currentRecordTime >= m_ucRecordLengthinSeconds && m_ucRecordLengthinSeconds != 0) {
        recordTimer->stop();
        recordStop();
        receiveMessage("Recording Stopped.");
        rootObject->setProperty("recording", false);
        currentRecordTime = 0;
    }

}

void ControlPanel::handleNoteSumbit(QString note)
{
    // Takes note submission from control panel GUI and sends it as a signal to be handled by backend

    sendNote(note);
    receiveMessage("Note logged.");
}

void ControlPanel::extTriggerSwitchToggled2(bool checkedState)
{
    emit setExtTriggerTrackingState(checkedState);
    if (checkedState == true) {
        rootObject->setProperty("ucRecordLength", 0);

    }
    else {
        rootObject->setProperty("ucRecordLength", m_ucRecordLengthinSeconds);

    }
}

void ControlPanel::extTriggerTriggered(bool state)
{
    if (state == true) {
        receiveMessage("Trigger: HIGH");
        onRecordActivated();
    }
    else {
        receiveMessage("Trigger: LOW");
        onStopActivated();

    }
}

void ControlPanel::close()
{
    view->close();
}
