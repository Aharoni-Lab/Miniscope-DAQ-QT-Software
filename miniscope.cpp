#include "miniscope.h"
#include "newquickview.h"
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
#include <QVector>

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

    getMiniscopeConfig(m_ucMiniscope["deviceType"].toString()); // holds specific Miniscope configuration


    // Thread sade buffer stuff
    freeFrames = new QSemaphore;
    usedFrames = new QSemaphore;
    freeFrames->release(FRAME_BUFFER_SIZE);
    // -------------------------

    // Setup OpenCV camera stream
    miniscopeStream = new VideoStreamOCV;
    miniscopeStream->setCameraID(m_ucMiniscope["deviceID"].toInt());
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

    view->setWidth(m_cMiniscopes["width"].toInt() * m_ucMiniscope["windowScale"].toDouble(1));
    view->setHeight(m_cMiniscopes["height"].toInt() * m_ucMiniscope["windowScale"].toDouble(1));
    view->setTitle(m_ucMiniscope["deviceName"].toString("Miniscope " + QString::number(m_ucMiniscope["deviceID"].toInt())));
    view->setX(m_ucMiniscope["windowX"].toInt(1));
    view->setY(m_ucMiniscope["windowY"].toInt(1));
    view->show();
    // --------------------


    rootObject = view->rootObject();
    configureMiniscopeControls();
    vidDisplay = rootObject->findChild<VideoDisplay*>("vD");

    QObject::connect(view, &NewQuickView::closing, miniscopeStream, &VideoStreamOCV::stopSteam);
    QObject::connect(vidDisplay->window(), &QQuickWindow::beforeRendering, this, &Miniscope::sendNewFrame);

}

void Miniscope::connectSnS(){
    QObject::connect(rootObject, SIGNAL( vidPropChangedSignal(QString, double) ),
                         this, SLOT( handlePropCangedSignal(QString, double) ));
    QObject::connect(this, SIGNAL( setPropertyI2C(long, QVector<quint8>) ), miniscopeStream, SLOT( setPropertyI2C(long, QVector<quint8>) ));
}

void Miniscope::parseUserConfigMiniscope() {
    // Currently not needed. If arrays get added into JSON config then this might
}

void Miniscope::getMiniscopeConfig(QString deviceType) {
    QString jsonFile;
    QFile file;
    file.setFileName(":/deviceConfigs/miniscopes.json");
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    jsonFile = file.readAll();
    file.close();
    QJsonDocument d = QJsonDocument::fromJson(jsonFile.toUtf8());
    QJsonObject jObj = d.object();
    m_cMiniscopes = jObj[deviceType].toObject();

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

        if (m_ucMiniscope.contains(controlName[i])) // sets starting value if it is defined in user config
            values["startValue"] = m_ucMiniscope[controlName[i]].toDouble();

        keys = values.keys();
        if (controlItem) {
            for (int j = 0; j < keys.size(); j++) { // Set min, max, startValue, and stepSize in order found in 'format'
                if (keys[j] == "sendCommand") {
                    m_controlSendCommand[controlName[i]] = parseSendCommand(values["sendCommand"].toArray());
                }
                else {
                    if (values[keys[j]].isArray()) {
                        QJsonArray tempArray = values[keys[j]].toArray();
                        QVariantList tempVect;
                        for (int k = 0; k < tempArray.size(); k++) {
                            if (tempArray[k].isDouble())
                                tempVect.append(tempArray[k].toDouble());
                            if (tempArray[k].isString())
                                tempVect.append(tempArray[k].toString());
                        }
                        controlItem->setProperty(keys[j].toLatin1().data(), tempVect);
                    }
                    else if (values[keys[j]].isString()) {
                        controlItem->setProperty(keys[j].toLatin1().data(), values[keys[j]].toString());
                    }
                    else
                        controlItem->setProperty(keys[j].toLatin1().data(), values[keys[j]].toDouble());
                }
            }

        }
        else
            qDebug() << controlName[i] << " not found in qml file.";

    }
}

QVector<QMap<QString, int>> Miniscope::parseSendCommand(QJsonArray sendCommand)
{
    // Creates a QMap for handing future I2C/SPI slider value send commands
    QVector<QMap<QString, int>> output;
    QMap<QString, int> commandStructure;
    QJsonObject jObj;
    QStringList keys;

    for (int i = 0; i < sendCommand.size(); i++) {
        jObj = sendCommand[i].toObject();
        keys = jObj.keys();

        for (int j = 0; j < keys.size(); j++) {
                // -1 = controlValue, -2 = error
                commandStructure[keys[j]] = processString2Int(jObj[keys[j]].toString());
            }
        output.append(commandStructure);
    }
    return output;
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
            else if (s == "valueH")
                value = SEND_COMMAND_VALUE_H;
            else if (s == "valueL")
                value = SEND_COMMAND_VALUE_L;
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
    QVector<quint8> packet;
    QMap<QString, int> sendCommand;
    int tempValue;
    long preambleKey; // Holds a value that represents the address and reg

    // TODO: Handle int values greater than 8 bits
    for (int i = 0; i < m_controlSendCommand[type].length(); i++) {
        sendCommand = m_controlSendCommand[type][i];
        packet.clear();
        if (sendCommand["protocol"] == PROTOCOL_I2C) {
            packet.append(sendCommand["addressW"]);
            for (int j = 0; j < sendCommand["regLength"]; j++) {
                packet.append(sendCommand["reg" + QString::number(j)]);
            }
            for (int j = 0; j < sendCommand["dataLength"]; j++) {
                tempValue = sendCommand["data" + QString::number(j)];
                // TODO: Handle value1 through value3
                if (tempValue == SEND_COMMAND_VALUE_H) {
                    packet.append(((quint16)round(value))>>8);
                }
                else if (tempValue == SEND_COMMAND_VALUE_L) {
                    packet.append((quint8)round(value));
                }
                else
                    packet.append(tempValue);
            }
//        qDebug() << packet;
        preambleKey = 0;
        for (int k = 0; k < (sendCommand["regLength"]+1); k++)
            preambleKey |= (packet[k]&0xFF)<<(8*k);
        emit setPropertyI2C(preambleKey, packet);
        }
        else {
            qDebug() << sendCommand["protocol"] << " protocol for " << type << " not yet supported";
        }
    }

}
