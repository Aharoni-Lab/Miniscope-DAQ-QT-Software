#include "videodevice.h"
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

VideoDevice::VideoDevice(QObject *parent, QJsonObject ucDevice, qint64 softwareStartTime) :
    QObject(parent),
    m_camConnected(false),
    deviceStream(nullptr),
    rootObject(nullptr),
    vidDisplay(nullptr),
    m_previousDisplayFrameNum(0),
    m_acqFrameNum(new QAtomicInt(0)),
    m_daqFrameNum(new QAtomicInt(0)),
    m_headOrientationStreamState(false),
    m_headOrientationFilterState(false),
    m_roiIsDefined(false),
    m_extTriggerTrackingState(false),
    m_errors(0),
    m_softwareStartTime(softwareStartTime)

{
    m_roiBoundingBox[0] = -1;
    m_roiBoundingBox[1] = -1;
    m_roiBoundingBox[2] = -1;
    m_roiBoundingBox[3] = -1;

    m_traceDisplayStatus = false;
    m_ucDevice = ucDevice; // hold user config for this device
    parseUserConfigDevice();
    m_cDevice = getDeviceConfig(m_ucDevice["deviceType"].toString()); // holds specific Miniscope configuration

    // Thread safe buffer stuff
    freeFrames = new QSemaphore;
    usedFrames = new QSemaphore;
    freeFrames->release(FRAME_BUFFER_SIZE);
    // -------------------------

    // Setup OpenCV camera stream
    m_resolution = QSize(m_cDevice["width"].toInt(-1), m_cDevice["height"].toInt(-1));
    deviceStream = new VideoStreamOCV(nullptr, m_cDevice["width"].toInt(-1), m_cDevice["height"].toInt(-1), m_cDevice["pixelClock"].toDouble(-1));
    deviceStream->setDeviceName(m_deviceName);

    // Checks to make sure user config and miniscope device type are supporting BNO streaming
    if (m_ucDevice.contains("headOrientation")) {
        m_headOrientationStreamState = m_ucDevice["headOrientation"].toObject()["enabled"].toBool(false);
        m_headOrientationFilterState = m_ucDevice["headOrientation"].toObject()["filterBadData"].toBool(false);
    }
    // DEPRICATED
    if (m_ucDevice.contains("streamHeadOrientation")) {
        m_headOrientationStreamState = m_ucDevice["streamHeadOrientation"].toBool(false) && m_cDevice["headOrientation"].toBool(false);
        // TODO: Tell user this name/value is depricated
    }
    // ==========
    deviceStream->setHeadOrientationConfig(m_headOrientationStreamState, m_headOrientationFilterState);

    deviceStream->setIsColor(m_cDevice["isColor"].toBool(false));

    qDebug() << m_ucDevice;
    if (m_ucDevice.contains("deviceID") && !m_ucDevice["deviceID"].isNull()) {
        qDebug() << "Camera" << m_ucDevice["deviceID"].toInt();
        m_camConnected = deviceStream->connect2Camera(m_ucDevice["deviceID"].toInt());
    }
    else if (m_ucDevice.contains("videoPlayback")) {
        qDebug() << "VIDEO!!!";
        m_camConnected = deviceStream->connect2Video(m_ucDevice["videoPlayback"].toObject()["folderPath"].toString(),
                m_ucDevice["videoPlayback"].toObject()["filePrefix"].toString(),
                m_ucDevice["videoPlayback"].toObject()["frameRate"].toDouble());
    }
    if (m_camConnected == 0) {
        qDebug() << "Not able to connect and open " << m_ucDevice["deviceName"].toString();
    }
    else {
        // TODO: bnoBuffer isn't used for behavior cams. Think about how to get rid of it
        deviceStream->setBufferParameters(frameBuffer,
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
        deviceStream->moveToThread(videoStreamThread);

    //    QObject::connect(miniscopeStream, SIGNAL (error(QString)), this, SLOT (errorString(QString)));
        QObject::connect(videoStreamThread, SIGNAL (started()), deviceStream, SLOT (startStream()));
    //    QObject::connect(miniscopeStream, SIGNAL (finished()), videoStreamThread, SLOT (quit()));
    //    QObject::connect(miniscopeStream, SIGNAL (finished()), miniscopeStream, SLOT (deleteLater()));
        QObject::connect(videoStreamThread, SIGNAL (finished()), videoStreamThread, SLOT (deleteLater()));

        // Pass send message signal through
        QObject::connect(deviceStream, &VideoStreamOCV::sendMessage, this, &VideoDevice::sendMessage);

        // Handle request for reinitialization of commands
        QObject::connect(deviceStream, &VideoStreamOCV::requestInitCommands, this, &VideoDevice::handleInitCommandsRequest);

        // --- USED ONLY FOR MINISCOPE INITIALLY -------------------------------
        // Handle external triggering passthrough
        QObject::connect(this, &VideoDevice::setExtTriggerTrackingState, deviceStream, &VideoStreamOCV::setExtTriggerTrackingState);
        QObject::connect(deviceStream, &VideoStreamOCV::extTriggered, this, &VideoDevice::extTriggered);

        QObject::connect(this, &VideoDevice::startRecording, deviceStream, &VideoStreamOCV::startRecording);
        QObject::connect(this, &VideoDevice::stopRecording, deviceStream, &VideoStreamOCV::stopRecording);
        // ----------------------------------------------

        // Signal/Slots for handling LED toggling during external trigger
        // TODO: Should probably consolidate how these signals and slots interact and remove the above signal passthrough
        QObject::connect(this, &VideoDevice::setExtTriggerTrackingState, this, &VideoDevice::handleSetExtTriggerTrackingState);
        QObject::connect(this, &VideoDevice::startRecording, this, &VideoDevice::handleRecordStart);
        QObject::connect(this, &VideoDevice::stopRecording, this, &VideoDevice::handleRecordStop);
        // ----------------------------------------------

        // ---------------------------------------------------------------------

        connectSnS();

        // THIS SHOULD ONLY BE SENT TO MINISCOPE AND MINICAM DEVICES. USE TO US AN if isMiniCAM statement here
        sendInitCommands();

        videoStreamThread->start();

        // Short sleep to make i2c initialize commands be sent before loading in user config controls
        QThread::msleep(500);
    }
}


void VideoDevice::createView()
{
    if (m_camConnected != 0) {
        if (m_camConnected == 1)
             sendMessage(m_deviceName + " connected using Direct Show.");
        else if (m_camConnected == 2)
            sendMessage(m_deviceName + " couldn't connect using Direct Show. Using computer's default backend.");
        else if (m_camConnected == 3)
            sendMessage("Video file loaded.");

        qmlRegisterType<VideoDisplay>("VideoDisplay", 1, 0, "VideoDisplay");

        // Setup device window
//        const QUrl url(m_cBehavCam["qmlFile"].toString("qrc:/behaviorCam.qml"));
        const QUrl url(m_cDevice["qmlFile"].toString());
        view = new NewQuickView(url);

        view->setWidth(m_cDevice["width"].toInt() * m_ucDevice["windowScale"].toDouble(1));
        view->setHeight(m_cDevice["height"].toInt() * m_ucDevice["windowScale"].toDouble(1));

        view->setTitle(m_deviceName);
        view->setX(m_ucDevice["windowX"].toInt(1));
        view->setY(m_ucDevice["windowY"].toInt(1));

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

        // Maybe move this to miniscope class
        QObject::connect(rootObject, SIGNAL( dFFSwitchChanged(bool) ),
                             this, SLOT( handleDFFSwitchChange(bool) ));

        QObject::connect(rootObject, SIGNAL( saturationSwitchChanged(bool) ),
                             this, SLOT( handleSaturationSwitchChanged(bool) ));

        configureDeviceControls();
        vidDisplay = rootObject->findChild<VideoDisplay*>("vD");
        vidDisplay->setMaxBuffer(FRAME_BUFFER_SIZE);
        vidDisplay->setWindowScaleValue(m_ucDevice["windowScale"].toDouble(1));

        // Turn on or off show saturation display
        if (m_ucDevice["showSaturation"].toBool(false)) {
            vidDisplay->setShowSaturation(1);
            rootObject->findChild<QQuickItem*>("saturationSwitch")->setProperty("checked", true);
        }
        else {
            vidDisplay->setShowSaturation(0);
            rootObject->findChild<QQuickItem*>("saturationSwitch")->setProperty("checked", false);
        }

        // Set ROI Stuff
        QObject::connect(rootObject, SIGNAL( setRoiClicked() ), this, SLOT( handleSetRoiClicked()));

        // Add Trace ROI Stuff
        QObject::connect(rootObject, SIGNAL( addTraceRoiClicked() ), this, SLOT( handleAddTraceRoiClicked()));

        // Link up ROI signal and slot
        QObject::connect(vidDisplay, &VideoDisplay::newROISignal, this, &VideoDevice::handleNewROI);

        // Link up Add Trace ROI signal and slot
        QObject::connect(vidDisplay, &VideoDisplay::newAddTraceROISignal, this, &VideoDevice::handleAddNewTraceROI);

        QObject::connect(view, &NewQuickView::closing, deviceStream, &VideoStreamOCV::stopSteam);
        QObject::connect(vidDisplay->window(), &QQuickWindow::beforeRendering, this, &VideoDevice::sendNewFrame);

        sendMessage(m_deviceName + " is connected.");

        if (m_ucDevice.contains("ROI")) {
            vidDisplay->setROI({(int)round(m_roiBoundingBox[0] * m_ucDevice["windowScale"].toDouble(1)),
                                (int)round(m_roiBoundingBox[1] * m_ucDevice["windowScale"].toDouble(1)),
                                (int)round(m_roiBoundingBox[2] * m_ucDevice["windowScale"].toDouble(1)),
                                (int)round(m_roiBoundingBox[3] * m_ucDevice["windowScale"].toDouble(1)),
                                0});
        }

        setupDisplayObjectPointers();
        emit displayCreated(); // signal to classes inherating this class
    }
    else {
        sendMessage("Error: " + m_deviceName + " cannot connect to camera. Check deviceID.");
    }

}

void VideoDevice::connectSnS(){

    // Only used for MS devices
    QObject::connect(this, SIGNAL( setPropertyI2C(long, QVector<quint8>) ), deviceStream, SLOT( setPropertyI2C(long, QVector<quint8>) ));

}

void VideoDevice::defineDeviceAddrs()
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

void VideoDevice::parseUserConfigDevice() {
    // Currently not needed. If arrays get added into JSON config then this might
    m_deviceName = m_ucDevice["deviceName"].toString("VideoDevice " + QString::number(m_ucDevice["deviceID"].toInt()));
    m_compressionType = m_ucDevice["compression"].toString("None");

    if (m_ucDevice.contains("ROI")) {
        // User Config defines ROI Bounding Box
        m_roiIsDefined = true;
        m_roiBoundingBox[0] = m_ucDevice["ROI"].toObject()["leftEdge"].toInt(-1);
        m_roiBoundingBox[1] = m_ucDevice["ROI"].toObject()["topEdge"].toInt(-1);
        m_roiBoundingBox[2] = m_ucDevice["ROI"].toObject()["width"].toInt(-1);
        m_roiBoundingBox[3] = m_ucDevice["ROI"].toObject()["height"].toInt(-1);
        // TODO: Throw error is values are incorrect or missing
    }
//    else {
//        m_roiBoundingBox[0] = 0;
//        m_roiBoundingBox[1] = 0;
//        m_roiBoundingBox[2] = m_cDevice["width"].toInt(-1);
//        m_roiBoundingBox[3] = m_cDevice["height"].toInt(-1);
//    }
}

void VideoDevice::sendInitCommands()
{
    // Sends out the commands in the miniscope json config file under Initialize
    QVector<quint8> packet;
    long preambleKey;
    int tempValue;

    QVector<QMap<QString,int>> sendCommands = parseSendCommand(m_cDevice["initialize"].toArray());
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

QString VideoDevice::getCompressionType()
{
    return m_compressionType;
}

QJsonObject VideoDevice::getDeviceConfig(QString deviceType) {
    QString jsonFile;
    QFile file;
    QJsonObject jObj;
    bool status = false;
    m_deviceType = deviceType;
    file.setFileName("deviceConfigs/videoDevices.json");
    status = file.open(QIODevice::ReadOnly | QIODevice::Text);
    if (status == true) {
        jsonFile = file.readAll();
        file.close();
        QJsonDocument d = QJsonDocument::fromJson(jsonFile.toUtf8());
        jObj = d.object();
        return jObj[deviceType].toObject();
    }
    else {
        m_errors |= VIDEODEVICES_JSON_LOAD_FAIL;
        return jObj; // empty json object
    }

}

void VideoDevice::configureDeviceControls() {

    QQuickItem *controlItem; // Pointer to VideoPropertyControl in qml for each objectName
    QJsonObject values; // min, max, startingValue, and stepSize for each control used in 'j' loop
    QStringList keys;

    QJsonObject controlSettings = m_cDevice["controlSettings"].toObject(); // Get controlSettings from json

    if (controlSettings.isEmpty()) {
        qDebug() << "controlSettings missing from miniscopes.json for deviceType = " << m_deviceType;
        return;
    }
    QStringList controlName =  controlSettings.keys();
    for (int i = 0; i < controlName.length(); i++) { // Loop through controls
        controlItem = rootObject->findChild<QQuickItem*>(controlName[i]);
//        qDebug() << controlItem;
        values = controlSettings[controlName[i]].toObject();

        if (m_ucDevice.contains(controlName[i])) {// sets starting value if it is defined in user config
            if (m_ucDevice[controlName[i]].isDouble())
                values["startValue"] = m_ucDevice[controlName[i]].toDouble();
            if (m_ucDevice[controlName[i]].isString()) {
                values["startValue"] = m_ucDevice[controlName[i]].toString();
//                qDebug() << "START:" << values["startValue"];
            }
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
                        if (keys[j] == "startValue") {
                            if (controlName[i] == "led0") { // This is used to hold initial (and last known) LED value for toggling LED on and off using remote trigger
                                m_lastLED0Value = values["startValue"].toDouble();
                            }
                            // sends signal on initial setup of controls
                            emit onPropertyChanged(m_deviceName, controlName[i], values["startValue"].toVariant());

                        }
                    }
                }
            }

        }
        else
            qDebug() << controlName[i] << " not found in qml file.";
    }
}

QVector<QMap<QString, int>> VideoDevice::parseSendCommand(QJsonArray sendCommand)
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

int VideoDevice::processString2Int(QString s)
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

void VideoDevice::testSlot(QString type, double value)
{
    qDebug() << "IN SLOT!!!!! " << type << " is " << value;
}

void VideoDevice::sendNewFrame(){
//    vidDisplay->setProperty("displayFrame", QImage("C:/Users/DBAharoni/Pictures/Miniscope/Logo/1.png"));
    int f = *m_acqFrameNum;

    if (f > m_previousDisplayFrameNum) {
        m_previousDisplayFrameNum = f;

//        qDebug() << "Send frame = " << f;
        f = (f - 1)%FRAME_BUFFER_SIZE;

        // TODO: figure out what to do with webcams for dropped frames
        vidDisplay->setDroppedFrameCount(*m_daqFrameNum - *m_acqFrameNum);

        // This function can be overridden by child class to add additional functionality
        handleNewDisplayFrame(timeStampBuffer[f], frameBuffer[f], f, vidDisplay);

        vidDisplay->setBufferUsed(usedFrames->available());

        if (f > 0) // This is just a quick cheat so I don't have to wrap around for (f-1)
            vidDisplay->setAcqFPS(timeStampBuffer[f] - timeStampBuffer[f-1]); // TODO: consider changing name as this is now interframeinterval


    }
}

void VideoDevice::handleNewDisplayFrame(qint64 timeStamp, cv::Mat frame, int bufIdx, VideoDisplay* vidDisp)
{
//    cv::Mat tempMat1, tempMat2;
    QImage tempFrame2;
    cv::Mat tempFrame;
    // TODO: Think about where color to gray and vise versa should take place.
    if (frame.channels() == 1) {
        cv::cvtColor(frame, tempFrame, cv::COLOR_GRAY2BGR);
        tempFrame2 = QImage(tempFrame.data, tempFrame.cols, tempFrame.rows, tempFrame.step, QImage::Format_RGB888);
    }
    else
        tempFrame2 = QImage(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);

    vidDisp->setDisplayFrame(tempFrame2);
}

void VideoDevice::handlePropChangedSignal(QString type, double displayValue, double i2cValue, double i2cValue2)
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

void VideoDevice::handleTakeScreenShotSignal()
{
    // Is called when signal from qml GUI is triggered
    takeScreenShot(m_deviceName);
}

void VideoDevice::handleSaturationSwitchChanged(bool checked)
{
    vidDisplay->setShowSaturation(checked);
}

void VideoDevice::handleSetExtTriggerTrackingState(bool state)
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
void VideoDevice::handleRecordStart()
{
    // Turns on led0 if software is in external trigger configuration
    if (m_extTriggerTrackingState) {
        QQuickItem *controlItem; // Pointer to VideoPropertyControl in qml for each objectName
        controlItem = rootObject->findChild<QQuickItem*>("led0");
        controlItem->setProperty("startValue", m_lastLED0Value);
    }
}

void VideoDevice::handleRecordStop()
{
    // Turns off led0 if software is in external trigger configuration
    if (m_extTriggerTrackingState) {
        QQuickItem *controlItem; // Pointer to VideoPropertyControl in qml for each objectName
        controlItem = rootObject->findChild<QQuickItem*>("led0");
        controlItem->setProperty("startValue", 0);
    }
}

void VideoDevice::handleInitCommandsRequest()
{
    qDebug() << "Reinitializing device.";
    sendInitCommands();
}

void VideoDevice::handleSetRoiClicked()
{
    // TODO: Don't allow this if recording is active!!!!

    // We probably should reset video display to full resolution here before user input of ROI????

    // Tell videodisplay that we will need mouse actions and will need to draw ROI rectangle
    vidDisplay->setROISelectionState(true);


    // TODO: disable ROI Button

}

void VideoDevice::handleAddTraceRoiClicked()
{
    vidDisplay->addTraceROISelectionState(true);
}

void VideoDevice::handleNewROI(int leftEdge, int topEdge, int width, int height)
{
    m_roiIsDefined = true;
    // First scale the local position values to pixel values
    m_roiBoundingBox[0] = round(leftEdge/m_ucDevice["windowScale"].toDouble(1));
    m_roiBoundingBox[1] = round(topEdge/m_ucDevice["windowScale"].toDouble(1));
    m_roiBoundingBox[2] = round(width/m_ucDevice["windowScale"].toDouble(1));
    m_roiBoundingBox[3] = round(height/m_ucDevice["windowScale"].toDouble(1));

    if ((m_roiBoundingBox[0] + m_roiBoundingBox[2]) > m_cDevice["width"].toInt(-1)) {
        // Edge is off screen
        m_roiBoundingBox[2] = m_cDevice["width"].toInt(-1) - m_roiBoundingBox[0];
        sendMessage("Warning: Right edge of ROI drawn beyond right edge of video. If this is incorrect you can change the width and height values in deviceCnfigs/behaviorCams.json");
    }
    if ((m_roiBoundingBox[1] + m_roiBoundingBox[3]) > m_cDevice["height"].toInt(-1)) {
        // Edge is off screen
        m_roiBoundingBox[3] = m_cDevice["height"].toInt(-1) - m_roiBoundingBox[1];
        sendMessage("Warning: Bottm edge of ROI drawn beyond bottom edge of video. If this is incorrect you can change the width and height values in deviceCnfigs/behaviorCams.json");

    }

    sendMessage("ROI Set to [" + QString::number(m_roiBoundingBox[0]) + ", " +
            QString::number(m_roiBoundingBox[1]) + ", " +
            QString::number(m_roiBoundingBox[2]) + ", " +
            QString::number(m_roiBoundingBox[3]) + "]");

    // TODO: Correct ROI if out of bounds

}

void VideoDevice::handleAddNewTraceROI(int leftEdge, int topEdge, int width, int height)
{

}

void VideoDevice::close()
{
    if (m_camConnected)
        view->close();
}

