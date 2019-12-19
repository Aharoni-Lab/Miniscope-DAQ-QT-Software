#include "miniscope.h"
#include "videodisplay.h"

#include <QQuickView>
#include <QSemaphore>
#include <QObject>
#include <QTimer>
#include <QAtomicInt>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QQmlApplicationEngine>



Miniscope::Miniscope(QObject *parent, QJsonObject ucMiniscope) :
    QObject(parent),
    miniscopeStream(0),
    rootObject(0),
    vidDisplay(0),
    m_previousDisplayFrameNum(0),
    m_acqFrameNum(new QAtomicInt(0))

{

    m_ucMiniscope = ucMiniscope; // hold user config for this Miniscope
    parseUserConfigMiniscope();

    getMiniscopeConfig(); // holds specific Miniscope configuration


    // Thread sade buffer stuff
    freeFrames = new QSemaphore;
    usedFrames = new QSemaphore;
    freeFrames->release(FRAME_BUFFER_SIZE);
    // -------------------------

    // Setup OpenCV camera stream
    miniscopeStream = new VideoStreamOCV;
    miniscopeStream->setCameraID(m_deviceID);
    miniscopeStream->setBufferParameters(frameBuffer,FRAME_BUFFER_SIZE,freeFrames,usedFrames,m_acqFrameNum);
    // -----------------

    // Threading and connections for thread stuff
    videoStreamThread = new QThread;
    miniscopeStream->moveToThread(videoStreamThread);

//    QObject::connect(miniscopeStream, SIGNAL (error(QString)), this, SLOT (errorString(QString)));
    QObject::connect(videoStreamThread, SIGNAL (started()), miniscopeStream, SLOT (startStream()));
//    QObject::connect(miniscopeStream, SIGNAL (finished()), videoStreamThread, SLOT (quit()));
//    QObject::connect(miniscopeStream, SIGNAL (finished()), miniscopeStream, SLOT (deleteLater()));
    QObject::connect(videoStreamThread, SIGNAL (finished()), videoStreamThread, SLOT (deleteLater()));
    // ----------------------------------------------

    createView();
    connectSnS();

    videoStreamThread->start();
}

void Miniscope::createView()
{
    qmlRegisterType<VideoDisplay>("VideoDisplay", 1, 0, "VideoDisplay");

    // Setup Miniscope window
    // TODO: Check deviceType and log correct qml file
    const QUrl url(QStringLiteral("qrc:/miniscope.qml"));
    view = new NewQuickView(url);

    view->setWidth(m_cMiniscopes["width"].toInt());
    view->setHeight(m_cMiniscopes["height"].toInt());
    view->setTitle(m_deviceName);
    view->show();
    // --------------------


    rootObject = view->rootObject();
    configureMiniscopeControls();
    vidDisplay = rootObject->findChild<VideoDisplay*>("vD");

    QObject::connect(view, &NewQuickView::closing, miniscopeStream, &VideoStreamOCV::stopSteam);
    QObject::connect(vidDisplay->window(), &QQuickWindow::beforeRendering, this, &Miniscope::sendNewFrame);

        if (vidDisplay) {
            qDebug() << "Found vid display: " << vidDisplay->displayFrame().height();
//
        }

}

void Miniscope::connectSnS(){
    QObject::connect(rootObject, SIGNAL( vidPropChangedSignal(QString,double) ),
                         miniscopeStream, SLOT( setProperty(QString,double) ));
}

void Miniscope::parseUserConfigMiniscope() {

    m_deviceID = m_ucMiniscope["deviceID"].toInt();
    m_deviceName = m_ucMiniscope["deviceName"].toString();
    m_deviceType = m_ucMiniscope["deviceType"].toString();
}

void Miniscope::getMiniscopeConfig() {
    QString jsonFile;
    QFile file;
    file.setFileName(":/deviceConfigs/miniscopes.json");
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    jsonFile = file.readAll();
    file.close();
    QJsonDocument d = QJsonDocument::fromJson(jsonFile.toUtf8());
    QJsonObject jObj = d.object();
    m_cMiniscopes = jObj[m_deviceType].toObject();

}

void Miniscope::configureMiniscopeControls() {

    QQuickItem *controlItem; // Pointer to VideoPropertyControl in qml for each objectName
    QJsonArray values; // min, max, startingValue, and stepSize for each control used in 'j' loop

    QJsonObject controlSettings = m_cMiniscopes["controlSettings"].toObject(); // Get controlSettings from json

    if (controlSettings.isEmpty()) {
        qDebug() << "controlSettings missing from miniscopes.json for deviceType = " << m_deviceType;
        return;
    }
    QStringList controlName =  controlSettings.keys(); // holds the names of the controls for this Minsicope deviceType
    QJsonArray format = controlSettings["format"].toArray(); // holds the format of the contolSettings (min, max, startValue, stepSize)
    if (format.isEmpty()){
        qDebug() << "format is missing from controlSettings in miniscopes.json for deviceType = " << m_deviceType;
        return;
    }
    // controlName in json must match object name in qml
    for (int i = 0; i < controlName.length(); i++) { // Loop through controls
        controlItem = rootObject->findChild<QQuickItem*>(controlName[i]);
        values = controlSettings[controlName[i]].toArray();
        if (controlItem) {
            for (int j = 0; j < format.size(); j++) { // Set min, max, startValue, and stepSize in order found in 'format'
                controlItem->setProperty(format[j].toString().toLatin1().data(), values[j].toDouble());
            }

        }
        else
            qDebug() << controlName[0] << " not found in qml file.";

    }
}

void Miniscope::testSlot(QString type, double value)
{
    qDebug() << "IN SLOT!!!!! " << type << " is " << value;
}
void Miniscope::sendNewFrame(){
//    vidDisplay->setProperty("displayFrame", QImage("C:/Users/DBAharoni/Pictures/Miniscope/Logo/1.png"));
    int f = *m_acqFrameNum;

    if (f > m_previousDisplayFrameNum) {
        m_previousDisplayFrameNum = f;
        QImage tempFrame;
//        qDebug() << "Send frame = " << f;
        f = (f - 1)%FRAME_BUFFER_SIZE;
        tempFrame = QImage(frameBuffer[f].data, frameBuffer[f].cols, frameBuffer[f].rows, frameBuffer[f].step, QImage::Format_RGB888);
        vidDisplay->setDisplayFrame(tempFrame);
    }
}

