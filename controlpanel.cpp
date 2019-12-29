#include "controlpanel.h"


#include "newquickview.h"


#include <QObject>
#include <QGuiApplication>
#include <QScreen>
#include <QQuickItem>

ControlPanel::ControlPanel(QObject *parent) : QObject(parent)
{
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

}

void ControlPanel::receiveMessage(QString msg)
{
    // Add msg to textbox in controlPanel.qml
    messageTextArea->setProperty("text", messageTextArea->property("text").toString() + msg + "\n");
}

void ControlPanel::onRecordActivated()
{
    recordStart();
}

void ControlPanel::onStopActivated()
{
    recordStop();
}
