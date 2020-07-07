#include "miniscope.h"
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


Miniscope::Miniscope(QObject *parent, QJsonObject ucMiniscope) :
    QObject(parent),
    m_camConnected(false),
    miniscopeStream(nullptr),
    rootObject(nullptr),
    vidDisplay(nullptr),
    m_previousDisplayFrameNum(0),
    m_acqFrameNum(new QAtomicInt(0)),
    m_daqFrameNum(new QAtomicInt(0)),
    m_headOrientationStreamState(false),
    m_headOrientationFilterState(false),
    m_displatState("Raw"),
    baselineFrameBufWritePos(0),
    baselinePreviousTimeStamp(0),
    m_extTriggerTrackingState(false)

{

    m_ucMiniscope = ucMiniscope; // hold user config for this Miniscope
//    qDebug() << m_ucMiniscope["deviceType"].toString();
    parseUserConfigMiniscope();
    getMiniscopeConfig(m_ucMiniscope["deviceType"].toString()); // holds specific Miniscope configuration

    // Checks to make sure user config and miniscope device type are supporting BNO streaming
    if (m_ucMiniscope.contains("headOrientation")) {
        m_headOrientationStreamState = m_ucMiniscope["headOrientation"].toObject()["enable"].toBool(false);
        m_headOrientationFilterState = m_ucMiniscope["headOrientation"].toObject()["filterBadData"].toBool(false);

    }

    // DEPRICATED
    if (m_ucMiniscope.contains("streamHeadOrientation")) {
        m_headOrientationStreamState = m_ucMiniscope["streamHeadOrientation"].toBool(false) && m_cMiniscopes["headOrientation"].toBool(false);
        // TODO: Tell user this name/value is depricated
    }
    // ==========




    // Thread safe buffer stuff
    freeFrames = new QSemaphore;
    usedFrames = new QSemaphore;
    freeFrames->release(FRAME_BUFFER_SIZE);
    // -------------------------

    // Setup OpenCV camera stream
    miniscopeStream = new VideoStreamOCV(nullptr, m_cMiniscopes["width"].toInt(-1), m_cMiniscopes["height"].toInt(-1));
    miniscopeStream->setDeviceName(m_deviceName);

    miniscopeStream->setHeadOrientationConfig(m_headOrientationStreamState, m_headOrientationFilterState);

    miniscopeStream->setIsColor(m_cMiniscopes["isColor"].toBool(false));

    m_camConnected = miniscopeStream->connect2Camera(m_ucMiniscope["deviceID"].toInt());
    if (m_camConnected == 0) {
        qDebug() << "Not able to connect and open " << m_ucMiniscope["deviceName"].toString();
    }
    else {
        miniscopeStream->setBufferParameters(frameBuffer,
                                             timeStampBuffer,
                                             bnoBuffer,
                                             FRAME_BUFFER_SIZE,
                                             freeFrames,
                                             usedFrames,
                                             m_acqFrameNum,
                                             m_daqFrameNum);


        // -----------------

        // Threading and connections for thread stuff
        videoStreamThread = new QThread;
        miniscopeStream->moveToThread(videoStreamThread);

    //    QObject::connect(miniscopeStream, SIGNAL (error(QString)), this, SLOT (errorString(QString)));
        QObject::connect(videoStreamThread, SIGNAL (started()), miniscopeStream, SLOT (startStream()));
    //    QObject::connect(miniscopeStream, SIGNAL (finished()), videoStreamThread, SLOT (quit()));
    //    QObject::connect(miniscopeStream, SIGNAL (finished()), miniscopeStream, SLOT (deleteLater()));
        QObject::connect(videoStreamThread, SIGNAL (finished()), videoStreamThread, SLOT (deleteLater()));

        // Pass send message signal through
        QObject::connect(miniscopeStream, &VideoStreamOCV::sendMessage, this, &Miniscope::sendMessage);

        // Handle external triggering passthrough
        QObject::connect(this, &Miniscope::setExtTriggerTrackingState, miniscopeStream, &VideoStreamOCV::setExtTriggerTrackingState);
        QObject::connect(miniscopeStream, &VideoStreamOCV::extTriggered, this, &Miniscope::extTriggered);

        QObject::connect(this, &Miniscope::startRecording, miniscopeStream, &VideoStreamOCV::startRecording);
        QObject::connect(this, &Miniscope::stopRecording, miniscopeStream, &VideoStreamOCV::stopRecording);
        // ----------------------------------------------

        // Signal/Slots for handling LED toggling during external trigger
        // TODO: Should probably consolidate how these signals and slots interact and remove the above signal passthrough
        QObject::connect(this, &Miniscope::setExtTriggerTrackingState, this, &Miniscope::handleSetExtTriggerTrackingState);
        QObject::connect(this, &Miniscope::startRecording, this, &Miniscope::handleRecordStart);
        QObject::connect(this, &Miniscope::stopRecording, this, &Miniscope::handleRecordStop);
        // ----------------------------------------------

    //    createView();
        connectSnS();

        sendInitCommands();

        videoStreamThread->start();

        // Short sleep to make i2c initialize commands be sent before loading in user config controls
        QThread::msleep(500);
    }
}

void Miniscope::createView()
{
    if (m_camConnected != 0) {
        if (m_camConnected == 1)
             sendMessage(m_deviceName + " connected using Direct Show.");
        else if (m_camConnected == 2)
            sendMessage(m_deviceName + " couldn't connect using Direct Show. Using computer's default backend.");
        qmlRegisterType<VideoDisplay>("VideoDisplay", 1, 0, "VideoDisplay");

        // Setup Miniscope window
        // TODO: Check deviceType and log correct qml file
    //    const QUrl url("qrc:/" + m_deviceType + ".qml");
        const QUrl url(m_cMiniscopes["qmlFile"].toString());
        view = new NewQuickView(url);

        view->setWidth(m_cMiniscopes["width"].toInt() * m_ucMiniscope["windowScale"].toDouble(1));
        view->setHeight(m_cMiniscopes["height"].toInt() * m_ucMiniscope["windowScale"].toDouble(1));

        view->setTitle(m_deviceName);
        view->setX(m_ucMiniscope["windowX"].toInt(1));
        view->setY(m_ucMiniscope["windowY"].toInt(1));

#ifdef Q_OS_WINDOWS
        view->setFlags(Qt::Window | Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint);
#endif
        view->show();
        // --------------------

        rootObject = view->rootObject();

        QObject::connect(rootObject, SIGNAL( takeScreenShotSignal() ),
                             this, SLOT( handleTakeScreenShotSignal() ));
        QObject::connect(rootObject, SIGNAL( vidPropChangedSignal(QString, double, double, double) ),
                             this, SLOT( handlePropChangedSignal(QString, double, double, double) ));
        QObject::connect(rootObject, SIGNAL( dFFSwitchChanged(bool) ),
                             this, SLOT( handleDFFSwitchChange(bool) ));

        configureMiniscopeControls();
        vidDisplay = rootObject->findChild<VideoDisplay*>("vD");
        vidDisplay->setMaxBuffer(FRAME_BUFFER_SIZE);

        // Turn on or off show saturation display
        if (m_ucMiniscope["showSaturation"].toBool(true))
            vidDisplay->setShowSaturation(1);
        else
            vidDisplay->setShowSaturation(0);

        if (m_headOrientationStreamState)
            bnoDisplay = rootObject->findChild<QQuickItem*>("bno");

        QObject::connect(view, &NewQuickView::closing, miniscopeStream, &VideoStreamOCV::stopSteam);
        QObject::connect(vidDisplay->window(), &QQuickWindow::beforeRendering, this, &Miniscope::sendNewFrame);

        sendMessage(m_deviceName + " is connected.");
    }
    else {
        sendMessage("Error: " + m_deviceName + " cannot connect to camera. Check deviceID.");
    }

}

void Miniscope::connectSnS(){

    QObject::connect(this, SIGNAL( setPropertyI2C(long, QVector<quint8>) ), miniscopeStream, SLOT( setPropertyI2C(long, QVector<quint8>) ));

}

void Miniscope::defineDeviceAddrs()
{
    // Currently these values are not used in the code but are helpful for creating config files by hand
    deviceAddr["deser"] = 0xC0;
    deviceAddr["ser"] = 0xB0;
    deviceAddr["v3_DAC"] = 0b10011000;
    deviceAddr["MT9V032"] = 0xB8;
    deviceAddr["MT9M001"] = 0xBA;
    deviceAddr["ewlDriver"] = 0b11101110;
    deviceAddr["digPot_deserSide"] = 0b01011000;
    deviceAddr["digPot"] = 0b1010000;
    deviceAddr["v4_MCU"] = 0x20;
    deviceAddr["BNO"] = 0b0101000;
    deviceAddr["MT9P031"] = 0xBA;
    deviceAddr["DAQ_EEPROM"] = 0xA0;
    deviceAddr["DAQ_CONFIG_COMMAND"] = 0xFE;

}

void Miniscope::parseUserConfigMiniscope() {
    // Currently not needed. If arrays get added into JSON config then this might
    m_deviceName = m_ucMiniscope["deviceName"].toString("Miniscope " + QString::number(m_ucMiniscope["deviceID"].toInt()));
    m_compressionType = m_ucMiniscope["compression"].toString("None");
}

void Miniscope::sendInitCommands()
{
    // Sends out the commands in the miniscope json config file under Initialize
    QVector<quint8> packet;
    long preambleKey;
    int tempValue;

    QVector<QMap<QString,int>> sendCommands = parseSendCommand(m_cMiniscopes["initialize"].toArray());
    QMap<QString,int> command;

    for (int i = 0; i < sendCommands.length(); i++) {
        // Loop through send commands
        command = sendCommands[i];
        packet.clear();
        if (command["protocol"] == PROTOCOL_I2C) {
            preambleKey = 0;

            packet.append(command["addressW"]);
            preambleKey = (preambleKey<<8) | packet.last();

            for (int j = 0; j < command["regLength"]; j++) {
                packet.append(command["reg" + QString::number(j)]);
                preambleKey = (preambleKey<<8) | packet.last();
            }
            for (int j = 0; j < command["dataLength"]; j++) {
                tempValue = command["data" + QString::number(j)];
                packet.append(tempValue);
                preambleKey = (preambleKey<<8) | packet.last();
            }
//        qDebug() << packet;
//        preambleKey = 0;
//        for (int k = 0; k < (command["regLength"]+1); k++)
//            preambleKey |= (packet[k]&0xFF)<<(8*k);
        emit setPropertyI2C(preambleKey, packet);
        }
        else {
            qDebug() << command["protocol"] << " initialize protocol not yet supported";
        }

    }
}

QString Miniscope::getCompressionType()
{
    return m_compressionType;
}

void Miniscope::getMiniscopeConfig(QString deviceType) {
    QString jsonFile;
    QFile file;
    m_deviceType = deviceType;
//    file.setFileName(":/deviceConfigs/miniscopes.json");
    file.setFileName("./deviceConfigs/miniscopes.json");
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

        if (m_ucMiniscope.contains(controlName[i])) {// sets starting value if it is defined in user config
            if (m_ucMiniscope[controlName[i]].isDouble())
                values["startValue"] = m_ucMiniscope[controlName[i]].toDouble();
            if (m_ucMiniscope[controlName[i]].isString())
                values["startValue"] = m_ucMiniscope[controlName[i]].toString();
        }

        keys = values.keys();
        if (controlItem) {
            controlItem->setVisible(true);
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
                        if (keys[j] == "startValue")
                            // sends signal on initial setup of controls
                            emit onPropertyChanged(m_deviceName, controlName[i], values["startValue"].toVariant());
                    }
                    else { // remaining option is value is a double
                        controlItem->setProperty(keys[j].toLatin1().data(), values[keys[j]].toDouble());
                        if (keys[j] == "startValue")
                            if (controlName[i] == "led0") { // This is used to hold initial (and last known) LED value for toggling LED on and off using remote trigger
                                m_lastLED0Value = values["startValue"].toDouble();
                            }
                            // sends signal on initial setup of controls
                            emit onPropertyChanged(m_deviceName, controlName[i], values["startValue"].toVariant());
                    }
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
            if (jObj[keys[j]].isString())
                commandStructure[keys[j]] = processString2Int(jObj[keys[j]].toString());
            else if (jObj[keys[j]].isDouble())
                commandStructure[keys[j]] = jObj[keys[j]].toInt();
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
            else if (s == "valueH24")
                value = SEND_COMMAND_VALUE_H24;
            else if (s == "valueH16")
                value = SEND_COMMAND_VALUE_H16;
            else if (s == "valueH")
                value = SEND_COMMAND_VALUE_H;
            else if (s == "valueL")
                value = SEND_COMMAND_VALUE_L;
            else if (s == "value")
                value = SEND_COMMAND_VALUE;
            else if (s == "value2H")
                value = SEND_COMMAND_VALUE2_H;
            else if (s == "value2L")
                value = SEND_COMMAND_VALUE2_L;
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
    cv::Mat tempMat1, tempMat2;
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

        // Generate moving average baseline frame
        if ((timeStampBuffer[f] - baselinePreviousTimeStamp) > 100) {
            // update baseline frame buffer every ~500ms
            tempMat1 = frameBuffer[f].clone();
            tempMat1.convertTo(tempMat1, CV_32F);
            tempMat1 = tempMat1/(BASELINE_FRAME_BUFFER_SIZE);
            if (baselineFrameBufWritePos == 0) {
                baselineFrame = tempMat1;
            }
            else if (baselineFrameBufWritePos < BASELINE_FRAME_BUFFER_SIZE) {
                baselineFrame += tempMat1;
            }
            else {
                baselineFrame += tempMat1;
                baselineFrame -= baselineFrameBuffer[baselineFrameBufWritePos%BASELINE_FRAME_BUFFER_SIZE];
            }
            baselineFrameBuffer[baselineFrameBufWritePos % BASELINE_FRAME_BUFFER_SIZE] = tempMat1.clone();
            baselinePreviousTimeStamp = timeStampBuffer[f];
            baselineFrameBufWritePos++;
        }

        if (m_displatState == "Raw") {

            vidDisplay->setDisplayFrame(tempFrame2.copy());
        }
        else if (m_displatState == "dFF") {
            // TODO: Implement this better. I am sure it can be sped up a lot. Maybe do most of it in a shader
            tempMat2 = frameBuffer[f].clone();
            tempMat2.convertTo(tempMat2, CV_32F);
            cv::divide(tempMat2,baselineFrame,tempMat2);
            tempMat2 = ((tempMat2 - 1.0) + 0.5) * 255;
            tempMat2.convertTo(tempMat2, CV_8U);
            cv::cvtColor(tempMat2, tempFrame, cv::COLOR_GRAY2BGR);
            tempFrame2 = QImage(tempFrame.data, tempFrame.cols, tempFrame.rows, tempFrame.step, QImage::Format_RGB888);
            vidDisplay->setDisplayFrame(tempFrame2.copy());
        }

        vidDisplay->setBufferUsed(usedFrames->available());
        if (f > 0) // This is just a quick cheat so I don't have to wrap around for (f-1)
            vidDisplay->setAcqFPS(timeStampBuffer[f] - timeStampBuffer[f-1]); // TODO: consider changing name as this is now interframeinterval
        vidDisplay->setDroppedFrameCount(*m_daqFrameNum - *m_acqFrameNum);

        if (m_headOrientationStreamState) {
//            bnoDisplay->setProperty("heading", bnoBuffer[f*3+0]);
//            bnoDisplay->setProperty("roll", bnoBuffer[f*3+1]);
//            bnoDisplay->setProperty("pitch", bnoBuffer[f*3+2]);
//            if (bnoBuffer[f*4+0] != -1/16384.0 && bnoBuffer[f*4+1] != -1/16384.0 && bnoBuffer[f*4+2] != -1/16384.0 && bnoBuffer[f*4+3] != -1/16384.0) {
//            if (bnoBuffer[f*5+4] >= 0.05) {
//                sendMessage("Quat w: " + QString::number( bnoBuffer[f*5+0]));
//                sendMessage("Quat x: " + QString::number( bnoBuffer[f*5+1]));
//                sendMessage("Quat y: " + QString::number( bnoBuffer[f*5+2]));
//                sendMessage("Quat z: " + QString::number( bnoBuffer[f*5+3]));
//                sendMessage("n = " + QString::number( bnoBuffer[f*5+4]));
//            }
            if (bnoBuffer[f*5+4] < 0.05) { // Checks to see if norm of quat differs from 1 by 0.05
                // good data
                bnoDisplay->setProperty("badData", false);
                bnoDisplay->setProperty("qw", bnoBuffer[f*5+0]);
                bnoDisplay->setProperty("qx", bnoBuffer[f*5+1]);
                bnoDisplay->setProperty("qy", bnoBuffer[f*5+2]);
                bnoDisplay->setProperty("qz", bnoBuffer[f*5+3]);
            }
            else {
                // bad BNO data
                bnoDisplay->setProperty("badData", true);
            }
//            }
        }
//        qDebug() << bnoBuffer[f*3+0] << bnoBuffer[f*3+1] << bnoBuffer[f*3+2];
    }
}


void Miniscope::handlePropChangedSignal(QString type, double displayValue, double i2cValue, double i2cValue2)
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


        if (type == "led0") {// This will update the last known LED value for use when toggling LED on and off using external trigger
            if (m_extTriggerTrackingState == false || (m_extTriggerTrackingState == true && displayValue > 0)) {
                m_lastLED0Value = displayValue;
                emit onPropertyChanged(m_deviceName, type, QVariant(displayValue)); // This sends the change to the datasaver
            }
        }
        else {
            emit onPropertyChanged(m_deviceName, type, QVariant(displayValue)); // This sends the change to the datasaver
        }

        // TODO: Handle int values greater than 8 bits
        for (int i = 0; i < m_controlSendCommand[type].length(); i++) {
            sendCommand = m_controlSendCommand[type][i];
            packet.clear();
            if (sendCommand["protocol"] == PROTOCOL_I2C) {
                preambleKey = 0;

                packet.append(sendCommand["addressW"]);
                preambleKey = (preambleKey<<8) | packet.last();

                for (int j = 0; j < sendCommand["regLength"]; j++) {
                    packet.append(sendCommand["reg" + QString::number(j)]);
                    preambleKey = (preambleKey<<8) | packet.last();
                }
                for (int j = 0; j < sendCommand["dataLength"]; j++) {
                    tempValue = sendCommand["data" + QString::number(j)];
                    // TODO: Handle value1 through value3
                    if (tempValue == SEND_COMMAND_VALUE_H24) {
                        packet.append((static_cast<quint32>(round(i2cValue))>>24)&0xFF);
                    }
                    else if (tempValue == SEND_COMMAND_VALUE_H16) {
                        packet.append((static_cast<quint32>(round(i2cValue))>>16)&0xFF);
                    }
                    else if (tempValue == SEND_COMMAND_VALUE_H) {
                        packet.append((static_cast<quint32>(round(i2cValue))>>8)&0xFF);
                    }
                    else if (tempValue == SEND_COMMAND_VALUE_L) {
                        packet.append(static_cast<quint32>(round(i2cValue))&0xFF);
                    }
                    else if (tempValue == SEND_COMMAND_VALUE2_H) {
                        packet.append((static_cast<quint32>(round(i2cValue2))>>8)&0xFF);
                    }
                    else if (tempValue == SEND_COMMAND_VALUE2_L) {
                        packet.append(static_cast<quint32>(round(i2cValue2))&0xFF);
                    }
                    else {
                        packet.append(tempValue);
                        preambleKey = (preambleKey<<8) | packet.last();
                    }
                }
    //        qDebug() << packet;

//                for (int k = 0; k < (sendCommand["regLength"]+1); k++)
//                    preambleKey |= (packet[k]&0xFF)<<(8*k);
                emit setPropertyI2C(preambleKey, packet);
            }
            else {
                qDebug() << sendCommand["protocol"] << " protocol for " << type << " not yet supported";
            }
        }
    }
}

void Miniscope::handleTakeScreenShotSignal()
{
    // Is called when signal from qml GUI is triggered
    takeScreenShot(m_deviceName);
}

void Miniscope::handleDFFSwitchChange(bool checked)
{
    qDebug() << "Switch" << checked;
    if (checked)
        m_displatState = "dFF";
    else
        m_displatState = "Raw";
}

void Miniscope::handleSetExtTriggerTrackingState(bool state)
{
     m_extTriggerTrackingState = state;
     if (m_extTriggerTrackingState == true) {
         // Let's turn off the led0
         QQuickItem *controlItem; // Pointer to VideoPropertyControl in qml for each objectName
         controlItem = rootObject->findChild<QQuickItem*>("led0");
         controlItem->setProperty("startValue", 0);
     }
     else {
         // Let's turn led0 back on
         QQuickItem *controlItem; // Pointer to VideoPropertyControl in qml for each objectName
         controlItem = rootObject->findChild<QQuickItem*>("led0");
         controlItem->setProperty("startValue", m_lastLED0Value);
     }
}
void Miniscope::handleRecordStart()
{
    // Turns on led0 if software is in external trigger configuration
    if (m_extTriggerTrackingState) {
        QQuickItem *controlItem; // Pointer to VideoPropertyControl in qml for each objectName
        controlItem = rootObject->findChild<QQuickItem*>("led0");
        controlItem->setProperty("startValue", m_lastLED0Value);
    }
}

void Miniscope::handleRecordStop()
{
    // Turns off led0 if software is in external trigger configuration
    if (m_extTriggerTrackingState) {
        QQuickItem *controlItem; // Pointer to VideoPropertyControl in qml for each objectName
        controlItem = rootObject->findChild<QQuickItem*>("led0");
        controlItem->setProperty("startValue", 0);
    }
}


void Miniscope::close()
{
    if (m_camConnected)
        view->close();
}
