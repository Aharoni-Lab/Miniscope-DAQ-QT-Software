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
    view->setWidth(200);
    view->setHeight(height*0.9);
    view->setTitle("Control Panel");
    view->setX(1);
    view->setY(50);
    view->show();

    rootObject = view->rootObject();
    messageTextArea = rootObject->findChild<QQuickItem*>("messageTextArea");

    messageTextArea->setProperty("text", messageTextArea->property("text").toString() + "ansosandioas\n");
//    // --------------------


//    rootObject = view->rootObject();
//    configureMiniscopeControls();
//    vidDisplay = rootObject->findChild<VideoDisplay*>("vD");

//    QObject::connect(view, &NewQuickView::closing, miniscopeStream, &VideoStreamOCV::stopSteam);
//    QObject::connect(vidDisplay->window(), &QQuickWindow::beforeRendering, this, &Miniscope::sendNewFrame);
}

void ControlPanel::connectSnS()
{


}

void ControlPanel::receiveMessage(QString msg)
{
    // Add msg to textbox in controlPanel.qml
}
