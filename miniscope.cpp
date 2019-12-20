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

#

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
    QObject::connect(rootObject, SIGNAL( vidPropChangedSignal(QString, double) ),
                         this, SLOT( handlePropCangedSignal(QString, double) ));
    QObject::connect(this, SIGNAL( setPropertyI2C(unsigned int, unsigned int) ), miniscopeStream, SLOT( setPropertyI2C(unsigned int, unsigned int) ));
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
    QJsonObject values; // min, max, startingValue, and stepSize for each control used in 'j' loop
    QStringList keys;

    QJsonObject controlSettings = m_cMiniscopes["controlSettings"].toObject(); // Get controlSettings from json

    if (controlSettings.isEmpty()) {
        qDebug() << "controlSettings missing from miniscopes.json for deviceType = " << m_deviceType;
        return;
    }
    QStringList controlName =  controlSettings.keys();
    for (int i = 0; i < controlName.length(); i++) { // Loop through controls
        controlItem = rootObject->findChild<QQuickItem*>(controlName[i]);
//        qDebug() << controlItem;
        values = controlSettings[controlName[i]].toObject();
        keys = values.keys();
        if (controlItem) {
            for (int j = 0; j < keys.size(); j++) { // Set min, max, startValue, and stepSize in order found in 'format'
                if (keys[j] == "sendCommand") {
                    m_controlSendCommand[controlName[i]] = parseSendCommand(values["sendCommand"].toObject());
                }
                else {
                    controlItem->setProperty(keys[j].toLatin1().data(), values[keys[j]].toDouble());
                }
            }

        }
        else
            qDebug() << controlName[i] << " not found in qml file.";

    }
}

QMap<QString, unsigned int> Miniscope::parseSendCommand(QJsonObject sendCommand)
{
    // Creates a QMap for handing future I2C/SPI slider value send commands
    QMap<QString, unsigned int> commandStructure;
    QStringList keys = sendCommand.keys();

    for (int i = 0; i < keys.size(); i++) {
        // -1 = controlValue, -2 = error
        commandStructure[keys[i]] = processString2Int(sendCommand[keys[i]].toString());
    }
    // Handle 1 byte length register
    if (commandStructure["registerLength"] < 2)
        commandStructure["registerH"] = 0;

    // Handle data that is 1 or 2 bytes long
    if (commandStructure["dataLength"] < 2)
        commandStructure["data1"] = 0;
    if (commandStructure["dataLength"] < 3)
        commandStructure["data2"] = 0;
    return commandStructure;
}

int Miniscope::processString2Int(QString s)
{
    // Should return a uint8 type of value (0 to 255)
    bool ok = false;
    int value;
    int size = s.size();
    if (size == 0) {
        qDebug() << "No data in string to convert to int";
        value = SEND_COMMAND_ERROR;
        ok = false;
    }
    else if (s.left(2) == "0x"){
        // HEX
        value = s.right(size-2).toUInt(&ok, 16);
    }
    else if (s.left(2) == "0b"){
        // Binary
        value = s.right(size-2).toUInt(&ok, 2);
    }
    else {
        value = s.toUInt(&ok, 10);
//        qDebug() << "String is " << s;
        if (ok == false) {
            // This is then a string
            if (s == "I2C")
                value = PROTOCOL_I2C;
            else if (s == "SPI")
                value = PROTOCOL_SPI;
            else if (s == "value")
                value = SEND_COMMAND_VALUE;
            else
                value = SEND_COMMAND_ERROR;
            ok = true;
        }
    }

    if (ok == true)
        return value;
    else
        return SEND_COMMAND_ERROR;
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


void Miniscope::handlePropCangedSignal(QString type, double value)
{
    // type is the objectName of the control
    // value is the control value that was just updated
    unsigned int tempVal = value;
    QMap<QString, unsigned int> sendCommand;
    // preamble holds i2cADDR, register length (in bytes), register
    unsigned int preamble;
    // data holds data length (in bytes), and data
    unsigned int data;

    // TODO: Handle int values greater than 8 bits
    sendCommand = m_controlSendCommand[type];


    if (sendCommand["protocol"] == PROTOCOL_I2C) {
//        qDebug() << sendCommand["addressW"] << " " << (sendCommand["registerLength"]) << " " << (sendCommand["registerH"]) << " " << (sendCommand["registerL"]);

        preamble =  sendCommand["addressW"] |
                    (sendCommand["registerLength"]<<8) |
                    (sendCommand["registerH"]<<16) |
                    (sendCommand["registerL"]<<24);

        data =      sendCommand["dataLength"];
        if (sendCommand["data0"] == SEND_COMMAND_VALUE)
            data |= tempVal<<8;
        else
            data |= sendCommand["data0"]<<8;
        if (sendCommand["data1"] == SEND_COMMAND_VALUE)
            data |= tempVal<<16;
        else
            data |= sendCommand["data1"]<<16;
        if (sendCommand["data2"] == SEND_COMMAND_VALUE)
            data |= tempVal<<24;
        else
            data |= sendCommand["data2"]<<24;

        //TODO: make sure int to double conversion for 32bit int works without floating point error
//        qDebug() << "preamble: 0x" << QString::number(preamble,16) << ". data: 0x" << QString::number(data, 16);

        emit setPropertyI2C(preamble, data);
    }
    else {
        qDebug() << sendCommand["protocol"] << " protocol for " << type << " not yet supported";
    }

}
