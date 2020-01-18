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

ControlPanel::ControlPanel(QObject *parent, QJsonObject userConfig) :
    QObject(parent),
    currentRecordTime(0)
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
    view->show();

    rootObject = view->rootObject();
    messageTextArea = rootObject->findChild<QQuickItem*>("messageTextArea");

    recordTimeText = rootObject->findChild<QQuickItem*>("recordTimeText");

    m_ucRecordLengthinSeconds = m_userConfig["recordLengthinSeconds"].toInt(0);
    rootObject->setProperty("currentRecordTime", 0);
    rootObject->setProperty("ucRecordLength", m_ucRecordLengthinSeconds);
//    recordTimeText->setProperty("text", "----/" + QString::number(m_ucRecordLengthinSeconds) + " s");

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
    recordStart();
    currentRecordTime = 0;
    recordTimer->start(1000);
    receiveMessage("Recording Started.");
}

void ControlPanel::onStopActivated()
{
    recordStop();
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
        currentRecordTime = 0;
    }

}

void ControlPanel::handleNoteSumbit(QString note)
{
    // Takes note submission from control panel GUI and sends it as a signal to be handled by backend

    sendNote(note);
    receiveMessage("Note logged.");
}

void ControlPanel::close()
{
    view->close();
}
