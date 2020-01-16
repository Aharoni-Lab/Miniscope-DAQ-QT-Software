#include "behaviorcam.h"
#include "newquickview.h"
#include "videodisplay.h"

#include <QQuickView>
#include <QQuickItem>
#include <QSemaphore>
#include <QObject>
#include <QTimer>
#include <QAtomicInt>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QQmlApplicationEngine>
#include <QVector>

BehaviorCam::BehaviorCam(QObject *parent, QJsonObject ucBehavCam) :
    QObject(parent),
    behavCamStream(0),
    rootObject(0),
    vidDisplay(0),
    m_previousDisplayFrameNum(0),
    m_acqFrameNum(new QAtomicInt(0)),
//    m_daqFrameNum(new QAtomicInt(0)),
    m_streamHeadOrientationState(false)
{

    m_ucBehavCam = ucBehavCam; // hold user config for this Miniscope
    parseUserConfigBehavCam();

    getBehavCamConfig(m_ucBehavCam["deviceType"].toString()); // holds specific Miniscope configuration

    // Checks to make sure user config and miniscope device type are supporting BNO streaming
    m_streamHeadOrientationState = m_ucBehavCam["streamHeadOrientation"].toBool(false) && m_cBehavCam["headOrientation"].toBool(false);


    // Thread safe buffer stuff
    freeFrames = new QSemaphore;
    usedFrames = new QSemaphore;
    freeFrames->release(FRAME_BUFFER_SIZE);
    // -------------------------

    // Setup OpenCV camera stream
    behavCamStream = new VideoStreamOCV;
    behavCamStream->setDeviceName(m_deviceName);

    behavCamStream->setStreamHeadOrientation(m_streamHeadOrientationState);
    behavCamStream->setIsColor(m_cBehavCam["isColor"].toBool(false));

    if (!behavCamStream->connect2Camera(m_ucBehavCam["deviceID"].toInt()))
        qDebug() << "Not able to connect and open " << m_ucBehavCam["deviceName"].toString();

    behavCamStream->setBufferParameters(frameBuffer,
                                         timeStampBuffer,
                                         nullptr,
                                         FRAME_BUFFER_SIZE,
                                         freeFrames,
                                         usedFrames,
                                         m_acqFrameNum,
                                         nullptr);


    // -----------------

    // Threading and connections for thread stuff
    videoStreamThread = new QThread;
    behavCamStream->moveToThread(videoStreamThread);

//    QObject::connect(miniscopeStream, SIGNAL (error(QString)), this, SLOT (errorString(QString)));
    QObject::connect(videoStreamThread, SIGNAL (started()), behavCamStream, SLOT (startStream()));
//    QObject::connect(miniscopeStream, SIGNAL (finished()), videoStreamThread, SLOT (quit()));
//    QObject::connect(miniscopeStream, SIGNAL (finished()), miniscopeStream, SLOT (deleteLater()));
    QObject::connect(videoStreamThread, SIGNAL (finished()), videoStreamThread, SLOT (deleteLater()));

    // Pass send message signal through
    QObject::connect(behavCamStream, &VideoStreamOCV::sendMessage, this, &BehaviorCam::sendMessage);

    // Pass new Frame available through to parent
    QObject::connect(behavCamStream, &VideoStreamOCV::newFrameAvailable, this, &BehaviorCam::newFrameAvailable);
    // ----------------------------------------------

    connectSnS();

//    sendInitCommands();

    videoStreamThread->start();
}

void BehaviorCam::createView()
{
    qmlRegisterType<VideoDisplay>("VideoDisplay", 1, 0, "VideoDisplay");

    // Setup Miniscope window

    // TODO: Check deviceType and log correct qml file
//    const QUrl url("qrc:/" + m_deviceType + ".qml");
    const QUrl url("qrc:/behaviorCam.qml");
    view = new NewQuickView(url);

    view->setWidth(m_cBehavCam["width"].toInt() * m_ucBehavCam["windowScale"].toDouble(1));
    view->setHeight(m_cBehavCam["height"].toInt() * m_ucBehavCam["windowScale"].toDouble(1));

    view->setTitle(m_deviceName);
    view->setX(m_ucBehavCam["windowX"].toInt(1));
    view->setY(m_ucBehavCam["windowY"].toInt(1));

    view->show();
    // --------------------

    rootObject = view->rootObject();
    configureBehavCamControls();
    vidDisplay = rootObject->findChild<VideoDisplay*>("vD");
    vidDisplay->setMaxBuffer(FRAME_BUFFER_SIZE);

    QObject::connect(rootObject, SIGNAL( takeScreenShotSignal() ),
                         this, SLOT( handleTakeScreenShotSignal() ));
    QObject::connect(rootObject, SIGNAL( vidPropChangedSignal(QString, double, double) ),
                         this, SLOT( handlePropCangedSignal(QString, double, double) ));

    QObject::connect(view, &NewQuickView::closing, behavCamStream, &VideoStreamOCV::stopSteam);
    QObject::connect(vidDisplay->window(), &QQuickWindow::beforeRendering, this, &BehaviorCam::sendNewFrame);

}

void BehaviorCam::connectSnS(){

//    QObject::connect(this, SIGNAL( setPropertyI2C(long, QVector<quint8>) ), behavCamStream, SLOT( setPropertyI2C(long, QVector<quint8>) ));

}

void BehaviorCam::parseUserConfigBehavCam() {
    // Currently not needed. If arrays get added into JSON config then this might
    m_deviceName = m_ucBehavCam["deviceName"].toString("Behavior Cam " + QString::number(m_ucBehavCam["deviceID"].toInt()));
}

//void BehaviorCam::sendInitCommands()
//{
//    // Sends out the commands in the miniscope json config file under Initialize
//    QVector<quint8> packet;
//    long preambleKey;
//    int tempValue;

//    QVector<QMap<QString,int>> sendCommands = parseSendCommand(m_cBehavCam["initialize"].toArray());
//    QMap<QString,int> command;

//    for (int i = 0; i < sendCommands.length(); i++) {
//        // Loop through send commands
//        command = sendCommands[i];
//        packet.clear();
//        if (command["protocol"] == PROTOCOL_I2C) {
//            packet.append(command["addressW"]);
//            for (int j = 0; j < command["regLength"]; j++) {
//                packet.append(command["reg" + QString::number(j)]);
//            }
//            for (int j = 0; j < command["dataLength"]; j++) {
//                tempValue = command["data" + QString::number(j)];
//                packet.append(tempValue);
//            }
//        preambleKey = 0;
//        for (int k = 0; k < (command["regLength"]+1); k++)
//            preambleKey |= (packet[k]&0xFF)<<(8*k);
//        emit setPropertyI2C(preambleKey, packet);
//        }
//        else {
//            qDebug() << command["protocol"] << " initialize protocol not yet supported";
//        }

//    }
//}

void BehaviorCam::getBehavCamConfig(QString deviceType) {
    QString jsonFile;
    QFile file;
    m_deviceType = deviceType;
    file.setFileName(":/deviceConfigs/behaviorCams.json");
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    jsonFile = file.readAll();
    file.close();
    QJsonDocument d = QJsonDocument::fromJson(jsonFile.toUtf8());
    QJsonObject jObj = d.object();
    m_cBehavCam = jObj[deviceType].toObject();

}

void BehaviorCam::configureBehavCamControls() {

    QQuickItem *controlItem; // Pointer to VideoPropertyControl in qml for each objectName
    QJsonObject values; // min, max, startingValue, and stepSize for each control used in 'j' loop
    QStringList keys;

    QJsonObject controlSettings = m_cBehavCam["controlSettings"].toObject(); // Get controlSettings from json

    if (controlSettings.isEmpty()) {
        qDebug() << "controlSettings missing from miniscopes.json for deviceType = " << m_deviceType;
        return;
    }
    QStringList controlName =  controlSettings.keys();
    for (int i = 0; i < controlName.length(); i++) { // Loop through controls
        controlItem = rootObject->findChild<QQuickItem*>(controlName[i]);
//        qDebug() << controlItem;
        values = controlSettings[controlName[i]].toObject();

        if (m_ucBehavCam.contains(controlName[i])) // sets starting value if it is defined in user config
            values["startValue"] = m_ucBehavCam[controlName[i]].toDouble();

        keys = values.keys();
        if (controlItem) {
            for (int j = 0; j < keys.size(); j++) { // Set min, max, startValue, and stepSize in order found in 'format'
                if (keys[j] == "sendCommand") {
//                    m_controlSendCommand[controlName[i]] = parseSendCommand(values["sendCommand"].toArray());
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
                    else {
                        controlItem->setProperty(keys[j].toLatin1().data(), values[keys[j]].toDouble());
                        if (keys[j] == "startValue")
                            // sends signal on initial setup of controls
                            emit onPropertyChanged(m_deviceName, controlName[i], values["startValue"].toDouble());
                    }
                }
            }

        }
        else
            qDebug() << controlName[i] << " not found in qml file.";

    }
}

//QVector<QMap<QString, int>> BehaviorCam::parseSendCommand(QJsonArray sendCommand)
//{
//    // Creates a QMap for handing future I2C/SPI slider value send commands
//    QVector<QMap<QString, int>> output;
//    QMap<QString, int> commandStructure;
//    QJsonObject jObj;
//    QStringList keys;

//    for (int i = 0; i < sendCommand.size(); i++) {
//        jObj = sendCommand[i].toObject();
//        keys = jObj.keys();

//        for (int j = 0; j < keys.size(); j++) {
//                // -1 = controlValue, -2 = error
//            if (jObj[keys[j]].isString())
//                commandStructure[keys[j]] = processString2Int(jObj[keys[j]].toString());
//            else if (jObj[keys[j]].isDouble())
//                commandStructure[keys[j]] = jObj[keys[j]].toInt();
//        }
//        output.append(commandStructure);
//    }
//    return output;
//}

//int BehaviorCam::processString2Int(QString s)
//{
//    // Should return a uint8 type of value (0 to 255)
//    bool ok = false;
//    int value;
//    int size = s.size();
//    if (size == 0) {
//        qDebug() << "No data in string to convert to int";
//        value = SEND_COMMAND_ERROR;
//        ok = false;
//    }
//    else if (s.left(2) == "0x"){
//        // HEX
//        value = s.right(size-2).toUInt(&ok, 16);
//    }
//    else if (s.left(2) == "0b"){
//        // Binary
//        value = s.right(size-2).toUInt(&ok, 2);
//    }
//    else {
//        value = s.toUInt(&ok, 10);
////        qDebug() << "String is " << s;
//        if (ok == false) {
//            // This is then a string
//            if (s == "I2C")
//                value = PROTOCOL_I2C;
//            else if (s == "SPI")
//                value = PROTOCOL_SPI;
//            else if (s == "valueH")
//                value = SEND_COMMAND_VALUE_H;
//            else if (s == "valueL")
//                value = SEND_COMMAND_VALUE_L;
//            else if (s == "value")
//                value = SEND_COMMAND_VALUE;
//            else
//                value = SEND_COMMAND_ERROR;
//            ok = true;
//        }
//    }

//    if (ok == true)
//        return value;
//    else
//        return SEND_COMMAND_ERROR;
//}

void BehaviorCam::testSlot(QString type, double value)
{
    qDebug() << "IN SLOT!!!!! " << type << " is " << value;
}
void BehaviorCam::sendNewFrame(){
//    vidDisplay->setProperty("displayFrame", QImage("C:/Users/DBAharoni/Pictures/Miniscope/Logo/1.png"));
    int f = *m_acqFrameNum;

    if (f > m_previousDisplayFrameNum) {
        m_previousDisplayFrameNum = f;
        QImage tempFrame2;
//        qDebug() << "Send frame = " << f;
        f = (f - 1)%FRAME_BUFFER_SIZE;

        // TODO: Think about where color to gray and vise versa should take place.
        if (frameBuffer[f].channels() == 1) {
            cv::cvtColor(frameBuffer[f], tempFrame, cv::COLOR_GRAY2BGR);
            tempFrame2 = QImage(tempFrame.data, tempFrame.cols, tempFrame.rows, tempFrame.step, QImage::Format_RGB888);
        }
        else
            tempFrame2 = QImage(frameBuffer[f].data, frameBuffer[f].cols, frameBuffer[f].rows, frameBuffer[f].step, QImage::Format_RGB888);
        vidDisplay->setDisplayFrame(tempFrame2.copy());

        vidDisplay->setBufferUsed(usedFrames->available());
        if (f > 0) // This is just a quick cheat so I don't have to wrap around for (f-1)
            vidDisplay->setAcqFPS(timeStampBuffer[f] - timeStampBuffer[f-1]); // TODO: consider changing name as this is now interframeinterval
    }
}

void BehaviorCam::handlePropCangedSignal(QString type, double displayValue, double i2cValue)
{
    // type is the objectName of the control
    // value is the control value that was just updated
    QVector<quint8> packet;
    QMap<QString, int> sendCommand;
    int tempValue;
    long preambleKey; // Holds a value that represents the address and reg

    sendMessage(m_deviceName + " " + type + " changed to " + QString::number(displayValue) + ".");
    // Handle props that only affect the user display here
    if (type == "alpha"){
        vidDisplay->setAlpha(displayValue);
    }
    else if (type == "beta") {
        vidDisplay->setBeta(displayValue);
    }
    else {
        // Here handles prop changes that need to be sent over to the Miniscope

        // TODO: maybe add a check to make sure property successfully updates before signallng it has changed
    //    qDebug() << "Sending updated prop signal to backend";
        emit onPropertyChanged(m_deviceName, type, displayValue);

        // TODO: Handle int values greater than 8 bits
//        for (int i = 0; i < m_controlSendCommand[type].length(); i++) {
//            sendCommand = m_controlSendCommand[type][i];
//            packet.clear();
//            if (sendCommand["protocol"] == PROTOCOL_I2C) {
//                preambleKey = 0;

//                packet.append(sendCommand["addressW"]);
//                preambleKey = (preambleKey<<8) | packet.last();

//                for (int j = 0; j < sendCommand["regLength"]; j++) {
//                    packet.append(sendCommand["reg" + QString::number(j)]);
//                    preambleKey = (preambleKey<<8) | packet.last();
//                }
//                for (int j = 0; j < sendCommand["dataLength"]; j++) {
//                    tempValue = sendCommand["data" + QString::number(j)];
//                    // TODO: Handle value1 through value3
//                    if (tempValue == SEND_COMMAND_VALUE_H) {
//                        packet.append(((quint16)round(i2cValue))>>8);
//                    }
//                    else if (tempValue == SEND_COMMAND_VALUE_L) {
//                        packet.append((quint8)round(i2cValue));
//                    }
//                    else {
//                        packet.append(tempValue);
//                        preambleKey = (preambleKey<<8) | packet.last();
//                    }
//                }
//    //        qDebug() << packet;

////                for (int k = 0; k < (sendCommand["regLength"]+1); k++)
////                    preambleKey |= (packet[k]&0xFF)<<(8*k);
//                emit setPropertyI2C(preambleKey, packet);
//            }
//            else {
//                qDebug() << sendCommand["protocol"] << " protocol for " << type << " not yet supported";
//            }
//        }
    }
}

void BehaviorCam::handleTakeScreenShotSignal()
{
    // Is called when signal from qml GUI is triggered
    takeScreenShot(m_deviceName);
}

void BehaviorCam::close()
{
    view->close();
}
