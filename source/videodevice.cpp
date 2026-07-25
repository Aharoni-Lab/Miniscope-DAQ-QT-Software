#include "videodevice.h"

#ifdef Q_OS_MACOS
#include "videostreammac.h"
#endif
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
#include <QFile>   // Qt6: no longer pulled in transitively
#include <QQmlApplicationEngine>
#include <QVector>

VideoDevice::VideoDevice(QObject *parent, QJsonObject ucDevice, qint64 softwareStartTime, bool preferDirectControl) :
    QObject(parent),
    m_camConnected(false),
    view(nullptr),
    deviceStream(nullptr),
    m_preferDirectControlBackend(preferDirectControl),
    videoStreamThread(nullptr),
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
    // Hold messages emitted before the backend wires sendMessage to the
    // control panel - see takeEarlyMessages().
    QObject::connect(this, &VideoDevice::sendMessage, this, [this](QString msg) {
        if (m_holdEarlyMessages)
            m_earlyMessages.append(msg);
    });

    m_roiBoundingBox[0] = -1;
    m_roiBoundingBox[1] = -1;
    m_roiBoundingBox[2] = -1;
    m_roiBoundingBox[3] = -1;

    m_traceDisplayStatus = false;
    m_lutColormap = 1; // default to the green LUT when toggled on
    m_ucDevice = ucDevice; // hold user config for this device
    parseUserConfigDevice();
    m_cDevice = getDeviceConfig(m_ucDevice["deviceType"].toString()); // holds specific Miniscope configuration

    // Thread safe buffer stuff
    freeFrames = new QSemaphore;
    usedFrames = new QSemaphore;
    freeFrames->release(FRAME_BUFFER_SIZE);
    // -------------------------

    // Setup camera stream backend.
    m_resolution = QSize(m_cDevice["width"].toInt(-1), m_cDevice["height"].toInt(-1));
    const int devWidth = m_cDevice["width"].toInt(-1);
    const int devHeight = m_cDevice["height"].toInt(-1);
    const double devPixelClock = m_cDevice["pixelClock"].toDouble(-1);

    // Miniscopes need a direct-control backend where plain OpenCV can't reach
    // the DAQ's control channel (see videostreambase.h): libuvc on Linux (the
    // kernel uvcvideo driver caches UVC control reads), and the AVFoundation +
    // IOKit hybrid on macOS (AVFoundation exposes no UVC controls at all). A
    // real live camera (deviceID) is required - video-file playback always
    // uses OpenCV.
    deviceStream = nullptr;
    const bool liveCamera = m_ucDevice.contains("deviceID") && !m_ucDevice["deviceID"].isNull();
#if defined(HAVE_LIBUVC)
    if (m_preferDirectControlBackend && liveCamera) {
        deviceStream = new VideoStreamLibUVC(nullptr, devWidth, devHeight, devPixelClock);
        qDebug() << "Using libuvc capture backend for" << m_deviceName;
    }
#elif defined(Q_OS_MACOS)
    if (m_preferDirectControlBackend && liveCamera) {
        deviceStream = new VideoStreamMac(nullptr, devWidth, devHeight, devPixelClock);
        qDebug() << "Using AVFoundation+IOKit hybrid capture backend for" << m_deviceName;
    }
#endif
    Q_UNUSED(liveCamera);
    if (deviceStream == nullptr)
        deviceStream = new VideoStreamOCV(nullptr, devWidth, devHeight, devPixelClock);
    deviceStream->setDeviceName(m_deviceName);

    // Pass send message signal through. Wired BEFORE connect2Camera/connect2Video
    // so connect-time errors (wrong deviceID, device busy, resolve failures)
    // actually reach the UI instead of vanishing - historically only the
    // generic "cannot connect" message ever showed.
    QObject::connect(deviceStream, &VideoStreamBase::sendMessage, this, &VideoDevice::sendMessage);

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
                                             daqFrameNumBuffer,
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

        // Handle request for reinitialization of commands
        QObject::connect(deviceStream, &VideoStreamBase::requestInitCommands, this, &VideoDevice::handleInitCommandsRequest);

        // --- USED ONLY FOR MINISCOPE INITIALLY -------------------------------
        // Handle external triggering passthrough
        QObject::connect(this, &VideoDevice::setExtTriggerTrackingState, deviceStream, &VideoStreamBase::setExtTriggerTrackingState);
        QObject::connect(deviceStream, &VideoStreamBase::extTriggered, this, &VideoDevice::extTriggered);

        QObject::connect(this, &VideoDevice::startRecording, deviceStream, &VideoStreamBase::startRecording);
        QObject::connect(this, &VideoDevice::stopRecording, deviceStream, &VideoStreamBase::stopRecording);
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


VideoDevice::~VideoDevice()
{
    // Normally the stream thread was already joined by the session teardown
    // (backEnd::stopSessionThreads); this is a no-op then, and a safety net
    // for any other deletion path.
    stopAndJoinStream();
    // The stream object was moved to the (now finished) stream thread, so it
    // may be deleted from here.
    delete deviceStream;
    deviceStream = nullptr;
    if (view) {
        view->close();
        // Deferred: we may be inside a handler of one of the view's signals.
        view->deleteLater();
        view = nullptr;
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

        // Let the video display scale with the window, locked to the camera's
        // aspect ratio (the full-screen video quad would otherwise distort).
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->setMinimumSize(QSize(view->width() / 2, view->height() / 2));
        view->setLockedAspectRatio((qreal)view->width() / (qreal)view->height());

#ifdef Q_OS_WINDOWS
        // Resizable (border drag) + minimizable; no maximize since that would break
        // the locked aspect ratio.
        view->setFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint
                       | Qt::WindowMinimizeButtonHint);
#endif
        // Not shown here: the Acquire pane host either embeds the view as a
        // pane (WindowContainer) or floats it, per the saved layout.
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

        QObject::connect(rootObject, SIGNAL( lutSwitchChanged(bool) ),
                             this, SLOT( handleLutSwitchChanged(bool) ));

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

        // Display LUT (colormap) chosen in the user config. The colormap is stored
        // in m_lutColormap (used by the on-window "Apply LUT" toggle); the switch
        // starts on when the config selects a real LUT. "None"/absent keeps the
        // green default available for when the user toggles it.
        const QString lutName = m_ucDevice["lut"].toString("None");
        bool lutOn = true;
        if (lutName == "Red")          m_lutColormap = 2;
        else if (lutName == "Inferno") m_lutColormap = 3;
        else if (lutName == "Green")   m_lutColormap = 1;
        else { m_lutColormap = 1; lutOn = false; } // "None" / unrecognized
        vidDisplay->setLutMode(lutOn ? m_lutColormap : 0);
        QQuickItem *lutSwitch = rootObject->findChild<QQuickItem*>("lutSwitch");
        if (lutSwitch) // only the Miniscope window has the switch
            lutSwitch->setProperty("checked", lutOn);

        // Set ROI Stuff
        QObject::connect(rootObject, SIGNAL( setRoiClicked() ), this, SLOT( handleSetRoiClicked()));

        // Add Trace ROI Stuff
        QObject::connect(rootObject, SIGNAL( addTraceRoiClicked() ), this, SLOT( handleAddTraceRoiClicked()));

        // Link up ROI signal and slot
        QObject::connect(vidDisplay, &VideoDisplay::newROISignal, this, &VideoDevice::handleNewROI);

        // Link up Add Trace ROI signal and slot
        QObject::connect(vidDisplay, &VideoDisplay::newAddTraceROISignal, this, &VideoDevice::handleAddNewTraceROI);

        // (No stop-stream-on-close: closing a floating pane re-docks it in the
        // Acquire view; streams only stop when the session ends.)
        QObject::connect(vidDisplay->window(), &QQuickWindow::beforeRendering, this, &VideoDevice::sendNewFrame);

        // Render on frame arrival. beforeRendering only PULLS the newest frame
        // when the scene renders; the old windows kept the scene permanently
        // dirty with an infinite dummy animation, the shell does not - so each
        // captured frame must request a render pass itself. Queued to the GUI
        // thread via the vidDisplay context; window() is looked up live since
        // embedding/floating reparents the view.
        QObject::connect(deviceStream, &VideoStreamBase::newFrameAvailable, vidDisplay,
                         [this] {
                             if (vidDisplay && vidDisplay->window())
                                 vidDisplay->window()->update();
                         });

        // Keep the ROI overlay tracking the video as the window is resized.
        QObject::connect(vidDisplay, &QQuickItem::widthChanged, this, &VideoDevice::handleDisplayResized);

        sendMessage(m_deviceName + " is connected.");

        if (m_ucDevice.contains("ROI")) {
            const QSizeF scale = displayPerCameraScale();
            vidDisplay->setROI({(int)round(m_roiBoundingBox[0] * scale.width()),
                                (int)round(m_roiBoundingBox[1] * scale.height()),
                                (int)round(m_roiBoundingBox[2] * scale.width()),
                                (int)round(m_roiBoundingBox[3] * scale.height()),
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
    // .value() everywhere: QJsonObject's non-const operator[] INSERTS a null
    // for a missing key, and (keys being kept sorted) that insertion shifts
    // the index under any QJsonValueRef already taken from the same object.
    // With no "deviceID" in the config (file-playback devices), the old
    // ["deviceName"].toString(... ["deviceID"] ...) line hit exactly that:
    // the deviceName ref went stale and every playback device fell back to
    // "VideoDevice 0".
    m_deviceName = m_ucDevice.value("deviceName")
                       .toString("VideoDevice " + QString::number(m_ucDevice.value("deviceID").toInt()));
    m_compressionType = m_ucDevice.value("compression").toString("None");

    if (m_ucDevice.contains("ROI")) {
        // User Config defines ROI Bounding Box
        const QJsonObject roi = m_ucDevice.value("ROI").toObject();
        m_roiIsDefined = true;
        m_roiBoundingBox[0] = roi.value("leftEdge").toInt(-1);
        m_roiBoundingBox[1] = roi.value("topEdge").toInt(-1);
        m_roiBoundingBox[2] = roi.value("width").toInt(-1);
        m_roiBoundingBox[3] = roi.value("height").toInt(-1);
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
        qDebug() << "controlSettings missing from videoDevices.json for deviceType = " << m_deviceType;
        return;
    }
    QStringList controlName =  controlSettings.keys();
    for (int i = 0; i < controlName.length(); i++) { // Loop through controls
        controlItem = rootObject->findChild<QQuickItem*>(controlName[i]);
//        qDebug() << controlItem;
        values = controlSettings[controlName[i]].toObject();

        // Merge the catalog's optional "fineSteps" override block (issue #68:
        // e.g. V4 led0 at one hardware step per slider tick) when the user
        // config sets "<control>FineSteps": true. take() runs unconditionally
        // so the block is never forwarded to the QML item as a property.
        const QJsonObject fineSteps = values.take("fineSteps").toObject();
        if (m_ucDevice[controlName[i] + "FineSteps"].toBool()) {
            if (fineSteps.isEmpty()) {
                sendMessage("Warning: " + m_deviceName + " has " + controlName[i] + "FineSteps set, but "
                            + m_deviceType + " defines no fine-steps mapping for " + controlName[i]
                            + ". Using the default mapping.");
            }
            else {
                for (auto it = fineSteps.constBegin(); it != fineSteps.constEnd(); ++it)
                    values[it.key()] = it.value();
                sendMessage(m_deviceName + " " + controlName[i] + " is using fine hardware steps.");
            }
        }

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

        // DAQ-counted frames minus software-grabbed frames, rebased to the
        // current connection epoch so a reconnect (which restarts the DAQ frame
        // counter) doesn't pin this at "N/A". -1 => "N/A" (webcams, or before
        // the first DAQ counter read).
        vidDisplay->setDroppedFrameCount(deviceStream ? deviceStream->droppedFrameEstimate() : -1);

        // This function can be overridden by child class to add additional functionality
        handleNewDisplayFrame(timeStampBuffer[f], frameBuffer[f], f, vidDisplay);

        vidDisplay->setBufferUsed(usedFrames->available());

        if (f > 0) // This is just a quick cheat so I don't have to wrap around for (f-1)
            vidDisplay->setAcqFPS(timeStampBuffer[f] - timeStampBuffer[f-1]); // TODO: consider changing name as this is now interframeinterval


    }
}

void VideoDevice::handleNewDisplayFrame(qint64 /*timeStamp*/, cv::Mat frame, int /*bufIdx*/, VideoDisplay* vidDisp)
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

void VideoDevice::handleLutSwitchChanged(bool checked)
{
    // Apply the config-selected colormap, or grayscale (0) when toggled off.
    vidDisplay->setLutMode(checked ? m_lutColormap : 0);
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
    setWindowRecordingIndicator(true);

    // Turns on led0 if software is in external trigger configuration
    if (m_extTriggerTrackingState) {
        QQuickItem *controlItem; // Pointer to VideoPropertyControl in qml for each objectName
        controlItem = rootObject->findChild<QQuickItem*>("led0");
        controlItem->setProperty("startValue", m_lastLED0Value);
    }
}

void VideoDevice::handleRecordStop()
{
    setWindowRecordingIndicator(false);

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

QSizeF VideoDevice::displayPerCameraScale()
{
    // The video fills the display item, so display/camera gives pixels-per-camera-
    // pixel. Computed live so it stays correct as the window is resized; falls back
    // to the static config windowScale before the display exists.
    const double camW = m_cDevice["width"].toInt(-1);
    const double camH = m_cDevice["height"].toInt(-1);
    if (vidDisplay && camW > 0 && camH > 0 && vidDisplay->width() > 0 && vidDisplay->height() > 0)
        return QSizeF(vidDisplay->width() / camW, vidDisplay->height() / camH);

    const double s = m_ucDevice["windowScale"].toDouble(1);
    return QSizeF(s, s);
}

void VideoDevice::handleDisplayResized()
{
    // Reposition the committed ROI overlay for the new display size. The ROI is
    // stored in camera pixels (m_roiBoundingBox); scale it back to display pixels.
    if (!vidDisplay || m_roiBoundingBox[0] < 0)
        return;

    const QSizeF scale = displayPerCameraScale();
    vidDisplay->setROI({(int)round(m_roiBoundingBox[0] * scale.width()),
                        (int)round(m_roiBoundingBox[1] * scale.height()),
                        (int)round(m_roiBoundingBox[2] * scale.width()),
                        (int)round(m_roiBoundingBox[3] * scale.height()),
                        0});
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
    // Map the selection (in live display pixels) back to camera pixels using the
    // current display scale, so the ROI is correct even after the window is resized.
    const QSizeF scale = displayPerCameraScale();
    m_roiBoundingBox[0] = round(leftEdge / scale.width());
    m_roiBoundingBox[1] = round(topEdge / scale.height());
    m_roiBoundingBox[2] = round(width / scale.width());
    m_roiBoundingBox[3] = round(height / scale.height());

    if ((m_roiBoundingBox[0] + m_roiBoundingBox[2]) > m_cDevice["width"].toInt(-1)) {
        // Edge is off screen
        m_roiBoundingBox[2] = m_cDevice["width"].toInt(-1) - m_roiBoundingBox[0];
        sendMessage("Warning: Right edge of ROI drawn beyond right edge of video. If this is incorrect you can change the width and height values in deviceConfigs/videoDevices.json");
    }
    if ((m_roiBoundingBox[1] + m_roiBoundingBox[3]) > m_cDevice["height"].toInt(-1)) {
        // Edge is off screen
        m_roiBoundingBox[3] = m_cDevice["height"].toInt(-1) - m_roiBoundingBox[1];
        sendMessage("Warning: Bottm edge of ROI drawn beyond bottom edge of video. If this is incorrect you can change the width and height values in deviceConfigs/videoDevices.json");

    }

    sendMessage("ROI Set to [" + QString::number(m_roiBoundingBox[0]) + ", " +
            QString::number(m_roiBoundingBox[1]) + ", " +
            QString::number(m_roiBoundingBox[2]) + ", " +
            QString::number(m_roiBoundingBox[3]) + "]");

    // TODO: Correct ROI if out of bounds

}

void VideoDevice::handleAddNewTraceROI(int /*leftEdge*/, int /*topEdge*/, int /*width*/, int /*height*/)
{

}

void VideoDevice::close()
{
    if (m_camConnected)
        view->close();
}

QStringList VideoDevice::takeEarlyMessages()
{
    m_holdEarlyMessages = false;
    const QStringList messages = m_earlyMessages;
    m_earlyMessages.clear();
    return messages;
}

void VideoDevice::stopAndJoinStream()
{
    if (!m_camConnected || deviceStream == nullptr || videoStreamThread == nullptr)
        return;
    // Direct call from the GUI thread: stopStream() only sets the (atomic)
    // stop flag, so this is safe and does not depend on the stream thread's
    // event processing. The stream loop then exits and the thread finishes.
    deviceStream->stopStream();
    videoStreamThread->quit();
    if (!videoStreamThread->wait(3000)) {
        qWarning() << m_deviceName << "stream thread did not stop within 3s; leaking it";
        // The stream object still lives on the runaway thread; deleting it
        // (see ~VideoDevice) would race. Leak it along with its thread.
        deviceStream = nullptr;
    }
    // The thread deletes itself via its finished() -> deleteLater() connection.
    videoStreamThread = nullptr;
}

