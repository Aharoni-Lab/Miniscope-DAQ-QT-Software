#include "backend.h"
#include "monotonicclock.h"
#include "newquickview.h"
#include <QDebug>
#include <QSerialPortInfo>
#include <QSettings>
#include <QStorageInfo>
#include <QQmlEngine>
#include <QWindow>
#include <cmath>
#include <QFileDialog>
#include <QApplication>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QObject>
#include <QVariant>
#include <QDir>
#include <QFile>
#include <QVector>
#include <QUrl>
#include <QString>
#include <QDateTime>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "miniscope.h"
#include "behaviorcam.h"
#include "configvalidator.h"
#include "controlpanel.h"
#include "datasaver.h"
#include "behaviortracker.h"
#include "tracedisplay.h"

#ifdef Q_OS_WINDOWS
// DirectShow video-device enumeration for scanVideoDevices().
#define NOMINMAX
#include <dshow.h>
#endif

#ifdef Q_OS_LINUX
// V4L2 video-device enumeration for scanVideoDevices().
#include <QDir>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#endif

#ifdef Q_OS_MACOS
// AVFoundation camera enumeration for scanVideoDevices(); the list order is
// the CAP_AVFOUNDATION deviceID order.
#include "avfenumeratormac.h"
#include "miniscopeprotocol.h"
#endif

backEnd::backEnd(QObject *parent) :
    QObject(parent),
    m_versionNumber(""),
    m_buildInfo(""),
    m_userConfigFileName(""),
    m_userConfigOK(false),
    controlPanel(nullptr), // read by sessionControl() before any session runs
    traceDisplay(nullptr),
    behavTracker(nullptr)
{
#ifdef DEBUG
//    QString homePath = QDir::homePath();
    m_userConfigFileName = "./userConfigs/UserConfigExample.json";
//    loadUserConfigFile();
    handleUserConfigFileNameChanged();

//    setUserConfigOK(true);
#endif
    m_softwareStartTime = monotonicTimeMs();

    // User Config default values
    researcherName = "";
    dataDirectory = "";
    experimentName = "";
    animalName = "";
    dataStructureOrder = {"researcherName", "experimentName", "animalName", "date"};

    ucExperiment["type"] = "None";
//    ucMiniscopes = {"None"};
//    ucBehaviorCams = {"None"};
    ucBehaviorTracker["type"] = "None";
    ucTraceDisplay["type"] = "None";

    dataSaver = new DataSaver();
    dataSaverThread = nullptr;   // created in setupDataSaver(); quitting before
                                 // any config loads must not touch a wild pointer

    testCodecSupport();
    QString tempStr;
    for (int i = 0; i < m_availableCodec.length(); i++)
        m_availableCodecList += m_availableCodec[i] + ", ";

    m_availableCodecList = m_availableCodecList.chopped(2);
    for (int i = 0; i < unAvailableCodec.length(); i++)
        tempStr += unAvailableCodec[i] + ", ";

    // Build initial text in main screen -------
    QFile file;
    QString jsonFile;
    QJsonObject jObj;
    QStringList supportedDevices;
    file.setFileName("deviceConfigs/videoDevices.json");
    bool status = file.open(QIODevice::ReadOnly | QIODevice::Text);
    if (status == true) {
        jsonFile = file.readAll();
        file.close();
        QJsonDocument d = QJsonDocument::fromJson(jsonFile.toUtf8());
        jObj = d.object();
        m_deviceCatalog = jObj;   // retained for the user-config generator (Add Device)
        supportedDevices = jObj.keys();
    }

    QString initDisplayMessage;
    initDisplayMessage = "Select a User Configuration file. You can click the button above or just drag and drop a user config file here.\n\n";
    initDisplayMessage.append("Supported devices are:\n");
    for (int i=0; i < supportedDevices.length(); i++) {
        initDisplayMessage.append("\t" + supportedDevices[i] + "\n");
    }
    initDisplayMessage.append("More information on the devices can be found in /deviceConfigs/videoDevices.json.\n\n");
    initDisplayMessage.append("Available compression Codecs on your computer are:\n\t" + m_availableCodecList +
                              "\n\nUnavailable compression Codes on your computer are:\n\t" + tempStr.chopped(2));

            setUserConfigDisplay(initDisplayMessage);

//    QObject::connect(this, SIGNAL (userConfigFileNameChanged()), this, SLOT( handleUserConfigFileNameChanged() ));


    file.setFileName("deviceConfigs/userConfigProps.json");
    status = file.open(QIODevice::ReadOnly | QIODevice::Text);
    if (status == true) {
        jsonFile = file.readAll();
        file.close();
        QJsonDocument d = QJsonDocument::fromJson(jsonFile.toUtf8());
        m_configProps = d.object();
    }
    else {
        // Can't find config props file. Possibly throw an error/warning somewhere???
    }

}

void backEnd::setUserConfigFileName(const QString &input)
{
    const QUrl url(input);
    QString furl = url.toLocalFile();
    if (furl.contains(".json")) {
        if (furl != m_userConfigFileName) {
            m_userConfigFileName = furl;
            emit userConfigFileNameChanged(); // header shows the loaded path
        }

        handleUserConfigFileNameChanged();
    }
    else {
        setUserConfigDisplay("Must select a .json User Config File.");
    }
}

void backEnd::setUserConfigDisplay(const QString &input)
{
    if (input != m_userConfigDisplay) {
        m_userConfigDisplay = input;
        emit userConfigDisplayChanged();
    }
}

void backEnd::setAvailableCodecList(const QString &input)
{
    m_availableCodecList = input;
}

// Saved configs carry the schema version they were written by and a $schema
// pointer so editors (VS Code etc.) validate and autocomplete them. Only
// added at save time, so configs the user never saves are never touched.
static void stampConfigMetadata(QJsonObject &config)
{
    config["configVersion"] = 1;
    if (!config.contains("$schema"))
        config["$schema"] = "https://raw.githubusercontent.com/Aharoni-Lab/"
                            "Miniscope-DAQ-QT-Software/master/deviceConfigs/userConfigSchema.json";
}

void backEnd::saveConfigObjectAs(const QString &filePath)
{
    stampConfigMetadata(m_userConfig); // may add configVersion/$schema
    QJsonDocument d;
    d.setObject(m_userConfig);
    QFile file(filePath);
    if (file.open(QFile::WriteOnly | QFile::Text | QFile::Truncate)) {
        file.write(d.toJson());
        file.close();
        sendMessage("User config saved to " + filePath);

        // The saved file is now the loaded config: adopt its path (Save As /
        // first save of a New config) and reset the dirty baseline.
        if (m_userConfigFileName != filePath) {
            m_userConfigFileName = filePath;
            emit userConfigFileNameChanged();
        }
        m_savedConfig = m_userConfig;
        updateDirtyState();
        emit userConfigJsonChanged(); // stamping may have changed the config
    }
    else {
        sendMessage("ERROR: could not save user config to " + filePath);
    }
}

// ---------------------------------------------------------------------------
// User-config generator
//
// Lets the user build a valid user config in-app without starting from an example
// file. newUserConfig() synthesizes a complete default skeleton straight from the
// schema (deviceConfigs/userConfigProps.json); addDevice() inserts a device built
// from the matching schema template, enriched with sensible starting values pulled
// from the device catalog (deviceConfigs/videoDevices.json). Both then re-run the
// validity check (which enables Save / Run) and notify the form editor. All of the
// existing serialization and save machinery is reused unchanged.
// ---------------------------------------------------------------------------

QJsonValue backEnd::defaultForType(const QString &type)
{
    if (type == "Bool")
        return false;
    if (type == "Integer" || type == "Number" || type == "Double")
        return 0;
    if (type.startsWith("Array"))
        return QJsonArray();
    // String, DirPath, FilePath, MiniscopeDeviceType, CameraDeviceType, ...
    return QString("");
}

QJsonValue backEnd::defaultFromProps(const QJsonValue &propNode)
{
    const QJsonObject obj = propNode.toObject();

    // Leaf node: { "type": "<string>", "tips": "..." }. A branch node's "type"
    // child (e.g. behaviorTracker.type) is itself an object, so testing that the
    // "type" value is a string reliably tells leaves from branches.
    if (obj.value("type").isString())
        return defaultForType(obj.value("type").toString());

    // Branch node: recurse into each sub-property (skipping COMMENT keys).
    QJsonObject out;
    const QStringList keys = obj.keys();
    for (const QString &k : keys) {
        if (k.contains("COMMENT"))
            continue;
        out[k] = defaultFromProps(obj.value(k));
    }
    return out;
}

void backEnd::newUserConfig()
{
    QJsonObject cfg;
    const QStringList keys = m_configProps.keys();
    for (const QString &k : keys) {
        if (k.contains("COMMENT"))
            continue;
        cfg[k] = defaultFromProps(m_configProps.value(k));
    }

    // Seed sensible, non-empty defaults so a fresh config is immediately usable
    // instead of being full of blanks/zeros the user has to fill in.
    cfg["researcherName"] = "Researcher";
    cfg["experimentName"] = "Experiment";
    cfg["animalName"]     = "Animal";
    // Default the data directory. If MINISCOPE_DATA_DIR is set (e.g. by the Linux
    // AppImage's first-run folder prompt), use it; otherwise fall back to a "Data"
    // folder next to the running app (the working dir, where ./deviceConfigs is).
    const QString envDataDir = qEnvironmentVariable("MINISCOPE_DATA_DIR");
    cfg["dataDirectory"] = envDataDir.isEmpty() ? (QDir::currentPath() + "/Data")
                                                : envDataDir;

    cfg["directoryStructure"] = QJsonArray{ "researcherName", "experimentName",
                                            "animalName", "date", "time" };

    QJsonObject devices;
    devices["miniscopes"] = QJsonObject();
    devices["cameras"]    = QJsonObject();
    cfg["devices"] = devices;

    // Trace display: on by default with a real window size, so it actually appears
    // (a 0x0 window never shows). "type" has a single valid value.
    if (cfg.contains("traceDisplay")) {
        QJsonObject td = cfg.value("traceDisplay").toObject();
        td["enabled"]      = true;
        td["type"]         = "scrolling";
        td["windowX"]      = 100;
        td["windowY"]      = 100;
        td["windowWidth"]  = 600;
        td["windowHeight"] = 800;
        cfg["traceDisplay"] = td;
    }

    // Behavior tracker stays off (it needs an external Python/DLC-Live setup), but
    // give it sane non-zero values so the section isn't all blanks/zeros if enabled.
    if (cfg.contains("behaviorTracker")) {
        QJsonObject bt = cfg.value("behaviorTracker").toObject();
        bt["type"]           = "DeepLabCut-Live";
        bt["resize"]         = 0.5;
        bt["pCutoffDisplay"] = 0.3;
        bt["windowX"]        = 200;
        bt["windowY"]        = 100;
        bt["windowScale"]    = 0.75;
        QJsonObject op = bt["occupancyPlot"].toObject();
        op["numBinsX"] = 100;
        op["numBinsY"] = 100;
        bt["occupancyPlot"] = op;
        QJsonObject po = bt["poseOverlay"].toObject();
        po["numOfPastPoses"] = 6;
        po["markerSize"]     = 20;
        bt["poseOverlay"] = po;
        cfg["behaviorTracker"] = bt;
    }

    m_userConfig = cfg;
    m_userConfigFileName.clear();   // brand-new config: unseed the Save-As dialog
    emit userConfigFileNameChanged();
    m_savedConfig = QJsonObject();  // nothing on disk yet -> dirty until saved

    updateHasDevices();
    checkUserConfigForIssues();     // emits userConfigOKChanged() -> enables Save/Run
    updateDirtyState();
    emit userConfigJsonChanged();
}

void backEnd::enrichDeviceDefaults(QJsonObject &device, const QString &category,
                                   const QString &deviceType)
{
    device["deviceType"]     = deviceType;
    device["deviceID"]       = 0;
    device["showSaturation"] = true;
    device["framesPerFile"]  = 1000;
    device["windowScale"]    = 0.75;
    device["windowX"]        = 100;
    device["windowY"]        = 100;

    const QJsonObject cat = m_deviceCatalog.value(deviceType).toObject();

    // ROI defaults to the device's native resolution.
    if (device.contains("ROI")) {
        QJsonObject roi = device["ROI"].toObject();
        roi["leftEdge"] = 0;
        roi["topEdge"]  = 0;
        if (cat.contains("width"))  roi["width"]  = cat.value("width");
        if (cat.contains("height")) roi["height"] = cat.value("height");
        device["ROI"] = roi;
    }

    // Control settings (gain / frameRate / led0 / ewl): use the catalog's
    // startValue for whichever ones this device template actually has.
    const QJsonObject controls = cat.value("controlSettings").toObject();
    const QStringList controlKeys = { "gain", "frameRate", "led0", "ewl" };
    for (const QString &ck : controlKeys) {
        if (device.contains(ck) && controls.contains(ck)) {
            const QJsonValue sv = controls.value(ck).toObject().value("startValue");
            if (!sv.isUndefined())
                device[ck] = sv;
        }
    }

    // Head orientation follows the catalog flag; default the plotted axes.
    if (device.contains("headOrientation")) {
        QJsonObject ho = device["headOrientation"].toObject();
        ho["enabled"]       = cat.value("headOrientation").toBool(false);
        ho["filterBadData"] = false;
        ho["plotTrace"]     = QJsonArray{ "roll", "pitch", "yaw" };
        device["headOrientation"] = ho;
    }

    // Display LUT is grayscale by default (miniscope only).
    if (device.contains("lut"))
        device["lut"] = "None";

    // Compression: pick a host-supported codec, preferring a sensible default per
    // device class, then any available, then GREY as a last resort.
    if (device.contains("compression")) {
        const QString preferred = (category == "miniscopes") ? "FFV1" : "MJPG";
        QString codec;
        if (m_availableCodec.contains(preferred))
            codec = preferred;
        else if (!m_availableCodec.isEmpty())
            codec = m_availableCodec.first();
        else
            codec = "GREY";
        device["compression"] = codec;
    }
}

QStringList backEnd::deviceTypesForCategory(const QString &category) const
{
    // Split the catalog by each entry's qmlFile: camera-class devices (WebCam
    // variants, Minicam) share behaviorCam.qml, miniscopes use a Miniscope_*.qml.
    // Anything not identified as a camera is treated as a miniscope. Keys come back
    // sorted (QJsonObject orders them), matching the dropdown's previous ordering.
    const bool wantCamera = (category == "cameras");
    QStringList types;
    const QStringList keys = m_deviceCatalog.keys();
    for (const QString &k : keys) {
        const QString qml = m_deviceCatalog.value(k).toObject()
                                .value("qmlFile").toString();
        const bool isCamera = qml.contains("behaviorCam", Qt::CaseInsensitive);
        if (isCamera == wantCamera)
            types << k;
    }
    return types;
}

void backEnd::addDevice(const QString &category, const QString &deviceType,
                        const QString &deviceName, int deviceID)
{
    if (deviceName.trimmed().isEmpty() || deviceType.isEmpty())
        return;
    if (category != "miniscopes" && category != "cameras")
        return;

    QJsonObject devices = m_userConfig.value("devices").toObject();
    QJsonObject section = devices.value(category).toObject();
    if (section.contains(deviceName))
        return;   // names are kept unique within a category

    // Build the device from its schema template, then fill catalog-derived defaults.
    const QString templateKey = (category == "miniscopes") ? "miniscopeDeviceName"
                                                           : "cameraDeviceName";
    const QJsonValue tmpl = m_configProps.value("devices").toObject()
                                .value(category).toObject().value(templateKey);
    QJsonObject device = defaultFromProps(tmpl).toObject();
    enrichDeviceDefaults(device, category, deviceType);
    device["deviceID"] = deviceID;   // user-chosen ID from the Add-Device dialog

    section[deviceName]     = device;
    devices[category]       = section;
    m_userConfig["devices"] = devices;

    updateHasDevices();
    checkUserConfigForIssues();
    updateDirtyState();
    emit userConfigJsonChanged();
}

// Public entry (wired to the "Scan Devices" button): dispatch to the OS-specific
// scan at compile time. Each backend builds its own report because the device
// model differs (DirectShow index on Windows vs V4L2 capture/metadata nodes).
QString backEnd::scanVideoDevices()
{
#if defined(Q_OS_WINDOWS)
    return scanVideoDevicesWindows();
#elif defined(Q_OS_LINUX)
    return scanVideoDevicesLinux();
#elif defined(Q_OS_MACOS)
    return scanVideoDevicesMac();
#else
    return QStringLiteral("Device scan is not available on this platform.");
#endif
}

#if defined(Q_OS_WINDOWS) || defined(Q_OS_MACOS)
// Shared Scan-Devices report for the platforms whose enumerator returns a
// plain name list whose position == the config deviceID (Linux formats its
// own richer capture-vs-metadata report).
static QString formatDeviceScan(const QStringList &names, const QString &trailerNote)
{
    if (names.isEmpty())
        return QStringLiteral("No video devices detected.");
    QStringList lines;
    for (int i = 0; i < names.size(); i++)
        lines << QString("    deviceID %1:  %2")
                     .arg(i)
                     .arg(names[i].isEmpty() ? QStringLiteral("(unknown)") : names[i]);
    return QStringLiteral("Detected video devices:\n") + lines.join("\n")
           + QStringLiteral("\n\nUse these deviceID numbers in your user config.")
           + trailerNote;
}
#endif

#if defined(Q_OS_WINDOWS)
// Names of the connected DirectShow video-input devices, indexed the same way
// OpenCV's CAP_DSHOW backend orders them, so the position == the config deviceID.
// Reused by scanVideoDevicesWindows() and availableDeviceIDs().
QStringList backEnd::enumerateVideoDevices()
{
    QStringList names;
    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool balanceUninit = SUCCEEDED(hrInit); // S_OK or S_FALSE (already init)

    ICreateDevEnum *devEnum = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&devEnum));
    if (SUCCEEDED(hr) && devEnum) {
        IEnumMoniker *enumMon = nullptr;
        hr = devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumMon, 0);
        if (hr == S_OK && enumMon) { // S_FALSE => no devices in this category
            IMoniker *moniker = nullptr;
            while (enumMon->Next(1, &moniker, nullptr) == S_OK) {
                IPropertyBag *propBag = nullptr;
                QString name;
                if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&propBag)))) {
                    VARIANT var;
                    VariantInit(&var);
                    if (SUCCEEDED(propBag->Read(L"FriendlyName", &var, nullptr)) && var.bstrVal)
                        name = QString::fromWCharArray(var.bstrVal);
                    VariantClear(&var);
                    propBag->Release();
                }
                names << name;            // index in this list == deviceID
                moniker->Release();
            }
            enumMon->Release();
        }
        devEnum->Release();
    }
    if (balanceUninit)
        CoUninitialize();
    return names;
}

QString backEnd::scanVideoDevicesWindows()
{
    return formatDeviceScan(enumerateVideoDevices(),
                            QStringLiteral(" Note: a Miniscope might appear under a generic "
                                           "name (e.g. \"USB Video Device\")."));
}

#elif defined(Q_OS_LINUX)
QString backEnd::scanVideoDevicesLinux()
{
    // Enumerate /dev/videoN via V4L2. Each physical UVC camera usually exposes
    // two nodes (a capture node and a metadata node), so we query each node's
    // capabilities and mark which one to use as the deviceID.
    struct Node { int id; QString name; bool capture; };
    QVector<Node> nodes;

    QDir v4lDir(QStringLiteral("/sys/class/video4linux"));
    const QStringList entries = v4lDir.entryList(QStringList() << QStringLiteral("video*"),
                                                 QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool ok = false;
        const int id = entry.mid(5).toInt(&ok); // strip "video"
        if (!ok)
            continue;

        // Human-readable name from sysfs.
        QString name;
        QFile nameFile(QStringLiteral("/sys/class/video4linux/%1/name").arg(entry));
        if (nameFile.open(QIODevice::ReadOnly | QIODevice::Text))
            name = QString::fromUtf8(nameFile.readLine()).trimmed();

        // Capture capability via VIDIOC_QUERYCAP.
        bool isCapture = false;
        const QString devPath = QStringLiteral("/dev/%1").arg(entry);
        const int fd = ::open(devPath.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            struct v4l2_capability cap;
            if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
                const unsigned int caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
                                              ? cap.device_caps : cap.capabilities;
                isCapture = caps & V4L2_CAP_VIDEO_CAPTURE;
            }
            ::close(fd);
        }
        nodes.append({id, name.isEmpty() ? QStringLiteral("(unknown)") : name, isCapture});
    }

    if (nodes.isEmpty())
        return QStringLiteral("No video devices detected (no /dev/video* nodes).");

    std::sort(nodes.begin(), nodes.end(), [](const Node &a, const Node &b){ return a.id < b.id; });

    QStringList lines;
    for (const Node &n : nodes)
        lines << QString("    deviceID %1:  %2  %3")
                     .arg(n.id)
                     .arg(n.name)
                     .arg(n.capture ? QStringLiteral("[capture - use this]")
                                    : QStringLiteral("[metadata/other - not usable]"));
    return QStringLiteral("Detected video devices:\n") + lines.join("\n")
           + QStringLiteral("\n\nUse the [capture] deviceID for each device in your user "
                            "config. A single camera often lists two nodes; only the "
                            "[capture] one streams video.");
}
#elif defined(Q_OS_MACOS)
// One line per AVFoundation camera; the list position IS the OpenCV
// deviceID. USB cameras get their USB identity too, and a Miniscope DAQ is
// called out explicitly (it otherwise shows up under a generic UVC name).
QStringList backEnd::enumerateVideoDevices()
{
    QStringList names;
    const auto cameras = enumerateAvfCameras();
    for (const auto &cam : cameras) {
        QString label = cam.name;
        if (cam.isUsb && cam.vid == MiniscopeProtocol::kUsbVendorId
            && cam.pid == MiniscopeProtocol::kUsbProductId)
            label += QStringLiteral("  [Miniscope DAQ]");
        names << label;   // index in this list == deviceID
    }
    return names;
}

QString backEnd::scanVideoDevicesMac()
{
    return formatDeviceScan(enumerateVideoDevices(), QString());
}
#endif

QStringList backEnd::availableDeviceIDs()
{
    // IDs already assigned to a device in the config.
    QList<int> used;
    const QJsonObject devs = m_userConfig.value("devices").toObject();
    const QStringList cats = { QStringLiteral("miniscopes"), QStringLiteral("cameras") };
    for (const QString &cat : cats) {
        const QJsonObject section = devs.value(cat).toObject();
        const QStringList devNames = section.keys();
        for (const QString &n : devNames)
            used << section.value(n).toObject().value("deviceID").toInt();
    }

    // One entry per connected device (fall back to 0..15 if none detected), and
    // always at least one ID past the highest used so the list is never empty.
    QStringList names;
#if defined(Q_OS_WINDOWS) || defined(Q_OS_MACOS)
    names = enumerateVideoDevices();   // label each ID with the connected device name
#endif
    int maxIDs = names.isEmpty() ? 16 : names.size();
    int highestUsed = -1;
    for (int u : used)
        highestUsed = qMax(highestUsed, u);
    maxIDs = qMax(maxIDs, highestUsed + 2);

    QStringList out;
    for (int id = 0; id < maxIDs; id++) {
        if (used.contains(id))
            continue;
        if (id < names.size() && !names[id].isEmpty())
            out << QString("%1  (%2)").arg(id).arg(names[id]);   // e.g. "0  (Asus Webcam)"
        else
            out << QString::number(id);
    }
    return out;
}

void backEnd::loadUserConfigFile()
{
    int count;
    QStringList sList;
    QString s;

    QString jsonFile;
    QFile file;
    file.setFileName(m_userConfigFileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setUserConfigDisplay("Could not open User Config file: " + m_userConfigFileName
                             + " (" + file.errorString() + ")");
        return;
    }
    jsonFile = file.readAll();
    file.close();
    QJsonParseError parseError;
    QJsonDocument d = QJsonDocument::fromJson(jsonFile.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        // Previously a malformed file silently became an empty config; say
        // what is wrong and where instead.
        m_configCheckNotes = "Could not parse " + m_userConfigFileName + ": "
                             + parseError.errorString() + " at offset "
                             + QString::number(parseError.offset);
        emit configCheckNotesChanged();
        setUserConfigDisplay(m_configCheckNotes + "\n\n" + jsonFile);
        m_userConfig = QJsonObject();
        return;
    }
    m_userConfig = d.object();

    // Correct for old device structure in user config files
    QJsonObject tempObj;
    QJsonObject deviceObj = m_userConfig.value("devices").toObject();
    if (m_userConfig.value("devices").toObject()["miniscopes"].isArray()) {
        QJsonArray tempAry = m_userConfig.value("devices").toObject()["miniscopes"].toArray();
        QJsonObject miniObj;
        sList.clear();
        count = 0;
        for (int i=0; i < tempAry.size(); i++) {
            s = tempAry[i].toObject()["deviceName"].toString();

            // Forces name to be unique
            if (sList.contains(s)) {
                s.append(QString::number(count));
                count++;
            }
            sList.append(s);
//            tempAry[i].toObject().remove("deviceName");
            tempObj = tempAry[i].toObject();
            tempObj.remove("deviceName");
            miniObj[s] = tempObj;
        }
        deviceObj.remove("miniscopes");
        deviceObj["miniscopes"] = miniObj;

    }
    if (m_userConfig.value("devices").toObject()["cameras"].isArray()) {
        QJsonArray tempAry = m_userConfig.value("devices").toObject()["cameras"].toArray();
        QJsonObject camObj;
        sList.clear();
        count = 0;
        for (int i=0; i < tempAry.size(); i++) {
            s = tempAry[i].toObject()["deviceName"].toString();

            // Forces name to be unique
            if (sList.contains(s)) {
                s.append(QString::number(count));
                count++;
            }
            sList.append(s);
            tempObj = tempAry[i].toObject();
            tempObj.remove("deviceName");
            camObj[s] = tempObj;
        }
        deviceObj.remove("cameras");
        deviceObj["cameras"] = camObj;
    }
    m_userConfig["devices"] = deviceObj;

    // Canonical-key migration (deprecated spellings keep working; the
    // in-memory config only ever carries canonical keys) followed by JSON
    // Schema validation. Warnings only - extra keys are fine and nothing
    // here blocks Run; the point is that a typo'd key or wrong type is
    // reported instead of silently replaced by a default.
    const QStringList configNotes = checkUserConfig(m_userConfig);
    m_configCheckNotes = configNotes.join(QLatin1Char('\n'));
    emit configCheckNotesChanged();
    setUserConfigDisplay("User Config File Selected: " + m_userConfigFileName + "\n" + jsonFile);
}

void backEnd::onRunClicked()
{
    if (m_sessionActive)
        return; // a session is already running; endSession() first

    // m_userConfig is already current: the form editor writes every edit
    // straight into it (setConfigValue / applyRawConfigJson).
    parseUserConfig();
    updateHasDevices();
    checkUserConfigForIssues();
    if (m_userConfigOK) {
        // Fresh timeline origin per session: every device timestamp and trace
        // time axis is relative to this.
        m_softwareStartTime = monotonicTimeMs();

        constructUserConfigGUI();

        setupDataSaver(); // must happen after devices have been made

        // Pane descriptors must exist before sessionActive flips the shell to
        // the Acquire view, which builds its containers from them.
        rebuildSessionPanes();

        m_sessionActive = true;
        emit sessionActiveChanged();
    }
    else {
        // TODO: throw out error
    }

}

void backEnd::onRecordClicked()
{
    //TODO: tell dataSaver to start recording

    // TODO: start experiment running
}

void backEnd::exitClicked()
{
    // force: quitting mid-recording stops the recording cleanly first
    // (blocking stopRecording in stopSessionThreads), as the app always did.
    endSessionImpl(true);
    emit closeAll();
}

// End the acquisition session without quitting the app. Refuses while a
// recording is running - ending the session would trash the experiment; the
// user must Stop first.
void backEnd::endSession()
{
    if (m_recording) {
        sendMessage("Warning: recording in progress - press Stop before ending the session.");
        return;
    }
    endSessionImpl(false);
}

// Join every worker thread, then destroy the session's windows and objects and
// reset per-session state, so a (possibly different) config can be Run again in
// this process. Safe to call from the session's own QML (all views die via
// deleteLater).
void backEnd::endSessionImpl(bool force)
{
    if (!m_sessionActive)
        return;
    if (m_recording && !force)
        return;
    m_sessionActive = false;

    // Empty the pane list before touching any window: the Acquire view reacts
    // synchronously, destroying its WindowContainers, so no container still
    // references a window the teardown below is about to delete.
    clearSessionPanes();

    stopSessionThreads();

    // Device objects own their windows and their (now joined) stream threads;
    // ~VideoDevice closes the window and frees the stream. Deferred deletion
    // because this can be triggered from one of those windows' QML.
    for (Miniscope *m : miniscope) { m->close(); m->deleteLater(); }
    miniscope.clear();
    for (BehaviorCam *c : behavCam) { c->close(); c->deleteLater(); }
    behavCam.clear();

    if (controlPanel) {
        controlPanel->deleteLater(); // windowless; nothing to close
        controlPanel = nullptr;
        emit sessionControlChanged();
    }
    if (traceDisplay) {
        traceDisplay->deleteLater(); // ~TraceDisplayBackend closes the window
        traceDisplay = nullptr;
    }
    if (behavTracker) {
        behavTracker->deleteLater(); // ~BehaviorTracker closes window + frees worker
        behavTracker = nullptr;
    }

    // Commutator worker: its thread was stopped in stopSessionThreads(). Direct
    // delete after the join - moveToThread can only PUSH from the object's own
    // thread (even a finished one), and deleteLater on a dead event loop never
    // runs. Same pattern as ~VideoDevice's stream cleanup. If the thread failed
    // to stop (3s timeout) both are leaked, deliberately.
    if (commutatorThread && !commutatorThread->isRunning()) {
        delete commutator;
        commutatorThread->deleteLater(); // QThread object lives on the GUI thread
    }
    commutator = nullptr;
    commutatorThread = nullptr;

    // Recycle the DataSaver so the next session's setupDataSaver() starts from
    // a pristine saver on a fresh thread. Skipped (leaked, with the warning
    // already printed) if its thread failed to stop.
    if (dataSaverThread && !dataSaverThread->isRunning()) {
        dataSaverThread->deleteLater();
        dataSaverThread = nullptr;
        delete dataSaver; // its thread is joined; direct delete is the safe form
        dataSaver = new DataSaver();
    }

    setRecordingState(false); // stopSessionThreads stopped any recording
    emit sessionActiveChanged();
}

void backEnd::setRecordingState(bool recording)
{
    if (m_recording == recording)
        return;
    m_recording = recording;
    emit recordingChanged();
}

// Orderly per-session thread teardown, run before the session's objects are
// destroyed (endSession) - and therefore also on quit, which routes through
// endSession. Without this no worker thread was ever joined: the process
// exited while capture/saver threads were still touching buffers and Qt
// objects - a use-after-free lottery on every quit.
void backEnd::stopSessionThreads()
{
    // 1. A recording in progress stops properly first: drains the ring
    //    buffers and closes every file. Blocking invoke so the files are
    //    complete before the pipeline is torn down.
    if (dataSaverThread && dataSaverThread->isRunning() && dataSaver->isRecording())
        QMetaObject::invokeMethod(dataSaver, "stopRecording", Qt::BlockingQueuedConnection);

    // 2. Stop the capture loops and join their threads (frame producers go
    //    quiet before their consumers are stopped).
    for (int i = 0; i < miniscope.length(); i++)
        miniscope[i]->stopAndJoinStream();
    for (int i = 0; i < behavCam.length(); i++)
        behavCam[i]->stopAndJoinStream();

    // 2b. Commutator worker: disable the motor and stop its thread. Done after
    //     the Miniscopes are quiet so no more orientation samples are queued to
    //     it. Blocking invoke so {enable:false} is written before the port closes.
    if (commutatorThread && commutatorThread->isRunning()) {
        QMetaObject::invokeMethod(commutator, "stopRunning", Qt::BlockingQueuedConnection);
        commutatorThread->quit();
        if (!commutatorThread->wait(3000))
            qWarning() << "Commutator thread did not stop within 3s; leaking it";
    }

    // 3. Then the saver loop and its thread.
    if (dataSaverThread && dataSaverThread->isRunning()) {
        QMetaObject::invokeMethod(dataSaver, "stopRunning", Qt::QueuedConnection);
        dataSaverThread->quit();
        if (!dataSaverThread->wait(3000))
            qWarning() << "DataSaver thread did not stop within 3s; leaking it";
    }

    // 4. Behavior-tracker worker, if one was started.
    if (behavTracker)
        behavTracker->stopAndJoinWorker();
}

void backEnd::handleUserConfigFileNameChanged()
{
    loadUserConfigFile();
    // Freshly loaded == clean, even when loadUserConfigFile migrated an
    // old-format device section (the migration notes flag that separately).
    m_savedConfig = m_userConfig;
    updateDirtyState();
    parseUserConfig();
    updateHasDevices();
    checkUserConfigForIssues();
    emit userConfigJsonChanged();
}

// --- Form-editor API ---------------------------------------------------------
// The form mutates m_userConfig directly (QJson types are value types, so a
// nested write rebuilds the object chain up to the root). Keys the editor
// doesn't know about - COMMENT_* annotations, retired settings, lab notes -
// ride along untouched, which the JSON tree editor never managed.

QJsonValue backEnd::jsonWithValueAtPath(const QJsonValue &node, const QStringList &path,
                                        const QJsonValue &value)
{
    if (path.isEmpty())
        return value;
    QJsonObject obj = node.toObject(); // non-object intermediate -> becomes one
    obj[path.first()] = jsonWithValueAtPath(obj.value(path.first()), path.mid(1), value);
    return obj;
}

QJsonValue backEnd::jsonWithKeyRemoved(const QJsonValue &node, const QStringList &path)
{
    QJsonObject obj = node.toObject();
    if (path.size() <= 1) {
        obj.remove(path.value(0));
        return obj;
    }
    if (!obj.contains(path.first()))
        return node; // nothing to remove
    obj[path.first()] = jsonWithKeyRemoved(obj.value(path.first()), path.mid(1));
    return obj;
}

void backEnd::configEdited()
{
    // Same checks as a fresh load, minus the file I/O: advisory schema notes,
    // then the blocking checks that gate Run.
    const QStringList configNotes = checkUserConfig(m_userConfig);
    m_configCheckNotes = configNotes.join(QLatin1Char('\n'));
    emit configCheckNotesChanged();

    parseUserConfig();
    updateHasDevices();
    checkUserConfigForIssues();
    updateDirtyState();
    emit userConfigJsonChanged();
}

void backEnd::updateDirtyState()
{
    const bool dirty = (m_userConfig != m_savedConfig);
    if (m_configDirty == dirty)
        return;
    m_configDirty = dirty;
    emit configDirtyChanged();
}

void backEnd::setConfigValue(const QVariantList &path, const QVariant &value)
{
    QStringList keys;
    for (const QVariant &p : path)
        keys << p.toString();
    if (keys.isEmpty())
        return;

    QJsonValue v = QJsonValue::fromVariant(value);
    // QML numbers arrive as doubles; store integral values as JSON ints so
    // integer-typed schema fields (deviceID, framesPerFile, ...) stay integers.
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (d == std::floor(d) && std::abs(d) <= 9007199254740992.0)
            v = QJsonValue(static_cast<qint64>(d));
    }

    m_userConfig = jsonWithValueAtPath(m_userConfig, keys, v).toObject();
    configEdited();
}

void backEnd::removeConfigKey(const QVariantList &path)
{
    QStringList keys;
    for (const QVariant &p : path)
        keys << p.toString();
    if (keys.isEmpty())
        return;
    m_userConfig = jsonWithKeyRemoved(m_userConfig, keys).toObject();
    configEdited();
}

void backEnd::removeDevice(const QString &category, const QString &deviceName)
{
    removeConfigKey(QVariantList{QStringLiteral("devices"), category, deviceName});
}

QString backEnd::rawConfigJson() const
{
    return QString::fromUtf8(QJsonDocument(m_userConfig).toJson(QJsonDocument::Indented));
}

QString backEnd::applyRawConfigJson(const QString &text)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
    if (doc.isNull())
        return QStringLiteral("JSON parse error at offset %1: %2")
            .arg(err.offset).arg(err.errorString());
    if (!doc.isObject())
        return QStringLiteral("The config must be a JSON object.");
    m_userConfig = doc.object();
    configEdited();
    return QString();
}

QVariantList backEnd::availableSerialPorts() const
{
    QVariantList out;
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &p : ports) {
        QString label = p.portName();
        if (!p.description().isEmpty())
            label += QStringLiteral(" — ") + p.description();
        else if (!p.manufacturer().isEmpty())
            label += QStringLiteral(" — ") + p.manufacturer();
        out.append(QVariantMap{{QStringLiteral("name"), p.portName()},
                               {QStringLiteral("label"), label}});
    }
    return out;
}

// --- Acquire pane host ---------------------------------------------------------
// The session's windows stay what they always were - one QQuickView per device/
// panel, each with its own QML engine and (for video) a raw-GL underlay - but the
// Acquire view now embeds them as panes via WindowContainer, with pop-out to a
// floating window. The backend only describes the windows; QML owns the layout.

void backEnd::rebuildSessionPanes()
{
    m_sessionPanes.clear();
    auto addPane = [this](const QString &name, const QString &type,
                          QQuickView *view, double aspect) {
        if (!view)
            return; // device that never connected has no window
        // The views are parentless QObjects; without an explicit ownership
        // claim, exposing them to QML would let the JS GC delete them.
        QQmlEngine::setObjectOwnership(view, QQmlEngine::CppOwnership);
        m_sessionPanes.append(QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("type"), type},
            {QStringLiteral("window"), QVariant::fromValue(static_cast<QWindow *>(view))},
            {QStringLiteral("aspect"), aspect}});
    };

    for (int i = 0; i < miniscope.length(); i++)
        addPane(miniscope[i]->getDeviceName(), QStringLiteral("video"),
                miniscope[i]->deviceView(), miniscope[i]->displayAspectRatio());
    for (int i = 0; i < behavCam.length(); i++)
        addPane(behavCam[i]->getDeviceName(), QStringLiteral("video"),
                behavCam[i]->deviceView(), behavCam[i]->displayAspectRatio());
    if (traceDisplay)
        addPane(QStringLiteral("Traces"), QStringLiteral("trace"),
                traceDisplay->displayView(), 0);
    // (The behavior tracker keeps its own floating window for now.)

    emit sessionPanesChanged();
}

void backEnd::clearSessionPanes()
{
    // Hide first so released windows can't flash as top-levels mid-teardown.
    for (const QVariant &p : std::as_const(m_sessionPanes)) {
        if (auto *w = qvariant_cast<QWindow *>(p.toMap().value(QStringLiteral("window"))))
            w->setVisible(false);
    }
    m_sessionPanes.clear();
    emit sessionPanesChanged();
}

QVariantMap backEnd::sessionTelemetry() const
{
    QVariantMap out;
    out.insert(QStringLiteral("diskFreeBytes"),
               dataDirectory.isEmpty()
                   ? -1.0
                   : double(QStorageInfo(dataDirectory).bytesAvailable()));

    QVariantList devices;
    auto addDevice = [&devices](VideoDevice *d) {
        devices.append(QVariantMap{
            {QStringLiteral("name"), d->getDeviceName()},
            {QStringLiteral("frames"), d->acqFrameCount()},
            {QStringLiteral("dropped"), d->droppedFrameEstimate()},
            {QStringLiteral("bufferUsed"), d->bufferUsedCount()},
            {QStringLiteral("bufferSize"), d->getBufferSize()}});
    };
    for (int i = 0; i < miniscope.length(); i++)
        addDevice(miniscope[i]);
    for (int i = 0; i < behavCam.length(); i++)
        addDevice(behavCam[i]);
    out.insert(QStringLiteral("devices"), devices);
    return out;
}

void backEnd::setPaneEmbedded(QObject *paneWindow, bool embedded, double aspect)
{
    auto *view = qobject_cast<NewQuickView *>(paneWindow);
    if (!view)
        return;
    if (embedded) {
        // The pane letterboxes to the aspect itself; the window must follow
        // the container's geometry freely.
        view->setLockedAspectRatio(0);
        view->setMinimumSize(QSize(0, 0));
    } else {
        view->setParent(nullptr); // release from the container -> top-level again
        view->setFlags(Qt::Window);
        view->setLockedAspectRatio(aspect > 0 ? aspect : 0);
        view->setMinimumSize(QSize(240, 180));
        view->show();
        view->requestActivate();
    }
}

// Layouts are stored per config file per pane; the path is hashed into a
// clean settings key (the layout is machine-local state, not config content).
static QString paneSettingsGroup(const QString &configPath, const QString &paneName)
{
    return QStringLiteral("paneLayouts/%1/%2")
        .arg(QString::number(qHash(configPath), 16), paneName);
}

QVariantMap backEnd::paneLayout(const QString &paneName) const
{
    QSettings settings;
    settings.beginGroup(paneSettingsGroup(m_userConfigFileName, paneName));
    QVariantMap out;
    const QStringList keys = settings.childKeys();
    for (const QString &k : keys)
        out.insert(k, settings.value(k));
    return out;
}

void backEnd::savePaneLayout(const QString &paneName, const QVariantMap &state)
{
    QSettings settings;
    settings.beginGroup(paneSettingsGroup(m_userConfigFileName, paneName));
    for (auto it = state.constBegin(); it != state.constEnd(); ++it)
        settings.setValue(it.key(), it.value());
}

void backEnd::connectSnS()
{

    // Start and stop recording signals
    QObject::connect(controlPanel, SIGNAL( recordStart(QMap<QString,QVariant>)), dataSaver, SLOT (startRecording(QMap<QString,QVariant>)));
    QObject::connect(controlPanel, SIGNAL( recordStop()), dataSaver, SLOT (stopRecording()));
    QObject::connect((controlPanel), SIGNAL( sendNote(QString) ), dataSaver, SLOT ( takeNote(QString) ));

    // Trace window is optional; only tear it down on exit if it was created.
    if (traceDisplay)
        QObject::connect(this, SIGNAL( closeAll()), traceDisplay, SLOT (close()));

    QObject::connect(dataSaver, SIGNAL(sendMessage(QString)), controlPanel, SLOT( receiveMessage(QString)));
    // A recording that could not create/continue its files must drop the UI's
    // "Recording" state instead of pretending to record.
    QObject::connect(dataSaver, &DataSaver::recordingFailed, controlPanel, &ControlPanel::onRecordingFailed);

    // Track recording state at the session level: it gates endSession() and
    // drives the shell's indicators. All start paths (Record button, external
    // trigger) route through these ControlPanel signals.
    QObject::connect(controlPanel, &ControlPanel::recordStart, this,
                     [this](QMap<QString, QVariant>) { setRecordingState(true); });
    QObject::connect(controlPanel, &ControlPanel::recordStop, this,
                     [this] { setRecordingState(false); });
    QObject::connect(dataSaver, &DataSaver::recordingFailed, this,
                     [this] { setRecordingState(false); });

    for (int i = 0; i < miniscope.length(); i++) {
        // For triggering screenshots
        QObject::connect(miniscope[i], SIGNAL(takeScreenShot(QString)), dataSaver, SLOT( takeScreenShot(QString)));
        QObject::connect(this, SIGNAL( closeAll()), miniscope[i], SLOT (close()));

        QObject::connect(controlPanel, &ControlPanel::setExtTriggerTrackingState, miniscope[i], &Miniscope::setExtTriggerTrackingState);
        QObject::connect(miniscope[i], &Miniscope::extTriggered, controlPanel, &ControlPanel::extTriggerTriggered);

        QObject::connect(controlPanel, &ControlPanel::recordStart, miniscope[i], &Miniscope::startRecording);
        QObject::connect(controlPanel, &ControlPanel::recordStop, miniscope[i], &Miniscope::stopRecording);
    }
    for (int i = 0; i < behavCam.length(); i++) {
//        QObject::connect(behavCam[i], SIGNAL(sendMessage(QString)), controlPanel, SLOT( receiveMessage(QString)));
        // For triggering screenshots
        QObject::connect(behavCam[i], SIGNAL(takeScreenShot(QString)), dataSaver, SLOT( takeScreenShot(QString)));

        QObject::connect(this, SIGNAL( closeAll()), behavCam[i], SLOT (close()));

        // REC chip on the camera window. Display only: cameras must NOT get
        // the startRecording signal chain (VideoStreamOCV::startRecording
        // writes the Miniscope DAQ's UVC side-channel, which would visibly
        // change a real webcam's image).
        QObject::connect(controlPanel, &ControlPanel::recordStart, behavCam[i],
                         [cam = behavCam[i]](QMap<QString, QVariant>) {
                             cam->setWindowRecordingIndicator(true);
                         });
        QObject::connect(controlPanel, &ControlPanel::recordStop, behavCam[i],
                         [cam = behavCam[i]] { cam->setWindowRecordingIndicator(false); });
        QObject::connect(dataSaver, &DataSaver::recordingFailed, behavCam[i],
                         [cam = behavCam[i]] { cam->setWindowRecordingIndicator(false); });

//        if (behavTracker) {
//            QObject::connect(behavCam[i], SIGNAL(newFrameAvailable(QString, int)), behavTracker, SLOT( handleNewFrameAvailable(QString, int)));
//        }
    }
    if (behavTracker)
        QObject::connect(this, SIGNAL( closeAll()), behavTracker, SLOT (close()));
}

void backEnd::setupDataSaver()
{
    dataSaver->setUserConfig(m_userConfig);
    dataSaver->setRecord(false);
//    dataSaver->startRecording();

    for (int i = 0; i < miniscope.length(); i++) {
        dataSaver->setDataCompression(miniscope[i]->getDeviceName(), miniscope[i]->getCompressionType());
        dataSaver->setFrameBufferParameters(miniscope[i]->getDeviceName(),
                                            miniscope[i]->getFrameBufferPointer(),
                                            miniscope[i]->getTimeStampBufferPointer(),
                                            miniscope[i]->getBNOBufferPointer(),
                                            miniscope[i]->getDaqFrameNumBufferPointer(),
                                            miniscope[i]->getBufferSize(),
                                            miniscope[i]->getFreeFramesPointer(),
                                            miniscope[i]->getUsedFramesPointer(),
                                            miniscope[i]->getAcqFrameNumPointer());

        dataSaver->setHeadOrientationConfig(miniscope[i]->getDeviceName(), miniscope[i]->getHeadOrienataionStreamState(), miniscope[i]->getHeadOrienataionFilterState());
        dataSaver->setROI(miniscope[i]->getDeviceName(), miniscope[i]->getROI());
    }
    for (int i = 0; i < behavCam.length(); i++) {
        dataSaver->setDataCompression(behavCam[i]->getDeviceName(), behavCam[i]->getCompressionType());
        dataSaver->setFrameBufferParameters(behavCam[i]->getDeviceName(),
                                            behavCam[i]->getFrameBufferPointer(),
                                            behavCam[i]->getTimeStampBufferPointer(),
                                            nullptr,   // no BNO on behavior cams
                                            nullptr,   // no DAQ frame counter on behavior cams
                                            behavCam[i]->getBufferSize(),
                                            behavCam[i]->getFreeFramesPointer(),
                                            behavCam[i]->getUsedFramesPointer(),
                                            behavCam[i]->getAcqFrameNumPointer());
        dataSaver->setHeadOrientationConfig(behavCam[i]->getDeviceName(), false, false);
        dataSaver->setROI(behavCam[i]->getDeviceName(), behavCam[i]->getROI());
    }

    if (!ucBehaviorTracker.isEmpty()) {
        if (ucBehaviorTracker["enabled"].toBool(true)) {
            dataSaver->setPoseBufferParameters(behavTracker->getPoseBufferPointer(),
                                               behavTracker->getPoseFrameNumBufferPointer(),
                                               behavTracker->getPoseBufferSize(),
                                               behavTracker->getFreePosePointer(),
                                               behavTracker->getUsedPosePointer());
        }
    }

    dataSaverThread = new QThread;
    dataSaver->moveToThread(dataSaverThread);

    QObject::connect(dataSaverThread, SIGNAL (started()), dataSaver, SLOT (startRunning()));
    // TODO: setup start connections

    dataSaverThread->start();
}

void backEnd::testCodecSupport()
{
    // This function will test which codecs are supported on host's machine.
    // Probe into a temp file (then delete it) so codec detection never leaves a
    // stray "test.avi" behind in the working directory / distribution folder.
    cv::VideoWriter testVid;
    const QString probePath = QDir(QDir::tempPath()).filePath("miniscope_codec_probe.avi");
    const std::string probe = probePath.toStdString();
    QVector<QString> possibleCodec({"DIB ", "MJPG", "MJ2C", "XVID", "FFV1", "DX50", "FLV1", "H264", "I420","MPEG","mp4v", "0000", "LAGS", "ASV1", "GREY"});
    for (int i = 0; i < possibleCodec.length(); i++) {
        testVid.open(probe, cv::VideoWriter::fourcc(possibleCodec[i].toStdString()[0],possibleCodec[i].toStdString()[1],possibleCodec[i].toStdString()[2],possibleCodec[i].toStdString()[3]),
                20, cv::Size(640, 480), true);
        if (testVid.isOpened()) {
            m_availableCodec.append(possibleCodec[i]);
            qDebug() << "Codec" << possibleCodec[i] << "supported for color";
            testVid.release();
        }
        else
            unAvailableCodec.append(possibleCodec[i]);
    }
    QFile::remove(probePath);   // remove the throwaway probe file
}

// Recompute whether the config has any devices (the Run button is gated on this)
// and notify QML if it changed. Call after the config is mutated or (re)loaded.
void backEnd::updateHasDevices()
{
    const QJsonObject devs = m_userConfig.value("devices").toObject();
    const bool has = (devs.value("miniscopes").toObject().size()
                      + devs.value("cameras").toObject().size()) > 0;
    if (has != m_hasDevices) {
        m_hasDevices = has;
        emit hasDevicesChanged();
    }
}

bool backEnd::checkUserConfigForIssues()
{
    if (checkForUniqueDeviceNames() == false) {
        // Need to tell user that user config has error(s)
        setUserConfigOK(false);
        userConfigOKChanged();
        showErrorMessage();
    }
    else if (checkForCompression() == false) {
        // Need to tell user that user config has error(s)
        setUserConfigOK(false);
        userConfigOKChanged();
        showErrorMessageCompression();
    }
    else {
        setUserConfigOK(true);
        userConfigOKChanged();
    }
    return m_userConfigOK;
}

void backEnd::parseUserConfig()
{
    QJsonObject devices = m_userConfig.value("devices").toObject();
    QJsonArray tempArray;
    QJsonObject tempObj;
    QStringList s;
    int count = 0;

    // Main JSON header
    researcherName = m_userConfig.value("researcherName").toString();
    dataDirectory= m_userConfig.value("dataDirectory").toString();
    dataStructureOrder = m_userConfig.value("dataStructureOrder").toArray();
    experimentName = m_userConfig.value("experimentName").toString();
    animalName = m_userConfig.value("animalName").toString();

    // JSON subsections
    ucExperiment = m_userConfig.value("experiment").toObject();

    if (devices["miniscopes"].isArray()) {
        tempArray = devices["miniscopes"].toArray();
        s.clear();
        count = 0;
        for (int i=0; i < tempArray.size(); i++) {
            if (s.contains(tempArray[i].toObject()["deviceName"].toString())) {
                // make name unique
                s.append(tempArray[i].toObject()["deviceName"].toString() + QString::number(count));
                count++;
            }
            else {
                s.append(tempArray[i].toObject()["deviceName"].toString());
            }
            tempObj = tempArray[i].toObject();
            tempObj["deviceName"] = s.last();
            ucMiniscopes[s.last()] = tempObj;
        }
    }
    else if (devices["miniscopes"].isObject()) {
        s = devices["miniscopes"].toObject().keys();
        for (int i=0; i < s.length(); i++) {
            tempObj = devices["miniscopes"].toObject()[s[i]].toObject();
            tempObj["deviceName"] = s[i];
            ucMiniscopes[s[i]] = tempObj;
        }

    }
//    ucMiniscopes = devices["miniscopes"].toArray();

    if (devices["cameras"].isArray()) {
        tempArray = devices["cameras"].toArray();
        s.clear();
        count = 0;
        for (int i=0; i < tempArray.size(); i++) {
            if (s.contains(tempArray[i].toObject()["deviceName"].toString())) {
                // make name unique
                s.append(tempArray[i].toObject()["deviceName"].toString() + QString::number(count));
                count++;
            }
            else {
                s.append(tempArray[i].toObject()["deviceName"].toString());
            }
            tempObj = tempArray[i].toObject();
            tempObj["deviceName"] = s.last();
            ucBehaviorCams[s.last()] = tempObj;
        }
    }
    else if (devices["cameras"].isObject()) {
        s = devices["cameras"].toObject().keys();
        for (int i=0; i < s.length(); i++) {
            tempObj = devices["cameras"].toObject()[s[i]].toObject();
            tempObj["deviceName"] = s[i];
            ucBehaviorCams[s[i]] = tempObj;
        }

    }

//    ucBehaviorCams = devices["cameras"].toArray();

    ucBehaviorTracker = m_userConfig.value("behaviorTracker").toObject();
    ucTraceDisplay = m_userConfig.value("traceDisplay").toObject();
    ucCommutator = m_userConfig.value("commutator").toObject();


}

void backEnd::setupBehaviorTracker()
{
    for (int i = 0; i < behavCam.length(); i++) {
        behavTracker->setBehaviorCamBufferParameters(behavCam[i]->getDeviceName(),
                                                     behavCam[i]->getTimeStampBufferPointer(),
                                                     behavCam[i]->getFrameBufferPointer(),
                                                     behavCam[i]->getBufferSize(),
                                                     behavCam[i]->getAcqFrameNumPointer());
    }

    // Start behavior tracker thread
    behavTracker->startThread();
}

bool backEnd::checkForUniqueDeviceNames()
{
    bool repeatingDeviceName = false;
    QString tempName;
    QVector<QString> deviceNames;
    QStringList keys;

    keys = ucMiniscopes.keys();
    for (int i = 0; i < keys.length(); i++) {
        tempName = ucMiniscopes[keys[i]].toObject()["deviceName"].toString();
        if (!deviceNames.contains(tempName))
            deviceNames.append(tempName);
        else {
            repeatingDeviceName = true;
            break;
        }
    }

    keys = ucBehaviorCams.keys();
    for (int i = 0; i < keys.length(); i++) {
        tempName = ucBehaviorCams[keys[i]].toObject()["deviceName"].toString();
        if (!deviceNames.contains(tempName))
            deviceNames.append(tempName);
        else {
            repeatingDeviceName = true;
            break;
        }
    }

    if (repeatingDeviceName == true) {
        qDebug() << "Repeating Device Names!";
        return false;
    }
    else {
        return true;
    }
}

bool backEnd::checkForCompression()
{
    QString tempName;
    QStringList keys;

    keys = ucMiniscopes.keys();
    for (int i = 0; i < keys.length(); i++) {
        tempName = ucMiniscopes[keys[i]].toObject()["compression"].toString("Empty");
        if (!m_availableCodec.contains(tempName) && tempName != "Empty")
            return false;
    }

    keys = ucBehaviorCams.keys();
    for (int i = 0; i < keys.length(); i++) {
        tempName = ucBehaviorCams[keys[i]].toObject()["compression"].toString("Empty");
        if (!m_availableCodec.contains(tempName) && tempName != "Empty")
            return false;
    }
    return true;
}

void backEnd::constructUserConfigGUI()
{
    int idx;
    QStringList keys;

    // The session controller behind the QML session bar (windowless).
    controlPanel = new ControlPanel(this, m_userConfig);
    QObject::connect(this, SIGNAL (sendMessage(QString) ), controlPanel, SLOT( receiveMessage(QString)));
    emit sessionControlChanged();

    // Make trace display. Traces are only ever fed by miniscopes (BNO / head
    // orientation) or the behavior tracker, so don't open an empty trace window
    // for a config with no such source (e.g. webcam-only), even if it is enabled.
    const bool behaviorTrackerWillRun = !ucBehaviorTracker.isEmpty()
            && ucBehaviorTracker["enabled"].toBool(true)
            && !ucBehaviorCams.isEmpty();
    const bool traceDisplayHasSource = !ucMiniscopes.isEmpty() || behaviorTrackerWillRun;
    if (!ucTraceDisplay.isEmpty() && ucTraceDisplay["enabled"].toBool(true)
            && traceDisplayHasSource) {
        traceDisplay = new TraceDisplayBackend(NULL, ucTraceDisplay, m_softwareStartTime);
    }

    // Make Minsicope displays
    keys = ucMiniscopes.keys();
    for (idx = 0; idx < keys.length(); idx++) {
        miniscope.append(new Miniscope(this, ucMiniscopes[keys[idx]].toObject(), m_softwareStartTime));
        QObject::connect(miniscope.last(),
                         SIGNAL (onPropertyChanged(QString, QString, QVariant)),
                         dataSaver,
                         SLOT (devicePropertyChanged(QString, QString, QVariant)));

        // Connect send and receive message to textbox in controlPanel
        QObject::connect(miniscope.last(), SIGNAL(sendMessage(QString)), controlPanel, SLOT( receiveMessage(QString)));
        // Relay messages the constructor emitted before this wiring existed
        // (connect failures name their cause there).
        for (const QString &msg : miniscope.last()->takeEarlyMessages())
            sendMessage(msg);
        // Qt6: connecting to a null receiver dereferences it (r->d_func() reads
        // offset 8 of nullptr) and crashes. traceDisplay is null unless a trace
        // display is configured + enabled, so guard the connection.
        if (traceDisplay)
            QObject::connect(miniscope.last(), &Miniscope::addTraceDisplay, traceDisplay, &TraceDisplayBackend::addNewTrace);
        if (miniscope.last()->getErrors() != 0) {
            // Errors have occured in creating this object
            sendMessage("ERROR: " + miniscope.last()->getDeviceName() + " has error: " + QString::number(miniscope.last()->getErrors()));
        }
        else {
            miniscope.last()->setTraceDisplayStatus(traceDisplay != nullptr);
            miniscope.last()->createView();
            miniscope.last()->setupBNOTraceDisplay();
        }
    }

    // Make Behav Cam displays
    keys = ucBehaviorCams.keys();
    for (idx = 0; idx < keys.length(); idx++) {
        behavCam.append(new BehaviorCam(this, ucBehaviorCams[keys[idx]].toObject(), m_softwareStartTime));
        QObject::connect(behavCam.last(),
                         SIGNAL (onPropertyChanged(QString, QString, QVariant)),
                         dataSaver,
                         SLOT (devicePropertyChanged(QString, QString, QVariant)));

        // Connect send and receive message to textbox in controlPanel
        QObject::connect(behavCam.last(), SIGNAL(sendMessage(QString)), controlPanel, SLOT( receiveMessage(QString)));
        // Relay messages the constructor emitted before this wiring existed
        // (connect failures name their cause there).
        for (const QString &msg : behavCam.last()->takeEarlyMessages())
            sendMessage(msg);

        if (behavCam.last()->getErrors() != 0) {
            // Errors have occured in creating this object
            sendMessage("ERROR: " + behavCam.last()->getDeviceName() + " has error: " + QString::number(behavCam.last()->getErrors()));
        }
        else
            behavCam.last()->createView();
    }

    // Create experiment interface
    if (!ucExperiment.isEmpty()){
        // Construct experiment interface
    }

    // Make behavior tracker interface
    if (!ucBehaviorTracker.isEmpty()) {
        if (ucBehaviorTracker["enabled"].toBool(true) && !behavCam.isEmpty()) {
            // Behav tracker currently is hardcoded to use first behavior camera
            QSize camRes = behavCam.first()->getResolution();

            behavTracker = new BehaviorTracker(NULL, m_userConfig, m_softwareStartTime);

            QObject::connect(behavTracker, SIGNAL(sendMessage(QString)), controlPanel, SLOT( receiveMessage(QString)));
            if (traceDisplay)  // Qt6: avoid connecting to a null receiver (crash)
                QObject::connect(behavTracker, &BehaviorTracker::addTraceDisplay, traceDisplay, &TraceDisplayBackend::addNewTrace);
            behavTracker->createView(camRes);
            setupBehaviorTracker();
        }
    }

    // Optional commutator (needs a Miniscope with head orientation to drive it,
    // so create it after the Miniscopes exist).
    setupCommutator();

    connectSnS();
}

// Create the commutator worker and put it on its own thread when the config asks
// for one. The BNO quaternion of the chosen source Miniscope is wired straight to
// it; from then on the worker owns the serial port and does all I/O off the GUI
// thread. Every early-return path explains itself in the message console rather
// than failing silently.
void backEnd::setupCommutator()
{
    if (ucCommutator.isEmpty() || !ucCommutator["enabled"].toBool(false))
        return;

    // Resolve the source Miniscope: the named one, else the first with head
    // orientation streaming enabled.
    const QString wanted = ucCommutator["deviceName"].toString();
    Miniscope *source = nullptr;
    for (Miniscope *m : miniscope) {
        if (!wanted.isEmpty()) {
            if (m->getDeviceName() == wanted) { source = m; break; }
        }
        else if (m->getHeadOrienataionStreamState()) {
            source = m;
            break;
        }
    }

    if (!source) {
        if (!wanted.isEmpty())
            sendMessage("ERROR: Commutator deviceName \"" + wanted
                        + "\" matches no configured Miniscope; commutator disabled.");
        else
            sendMessage("ERROR: Commutator needs a Miniscope with headOrientation enabled to "
                        "drive it, but none was found; commutator disabled.");
        return;
    }
    if (!source->getHeadOrienataionStreamState())
        sendMessage("Warning: Commutator source \"" + source->getDeviceName()
                    + "\" does not have headOrientation enabled, so no rotation data will flow.");

    commutator = new Commutator(nullptr, ucCommutator);
    QObject::connect(commutator, SIGNAL(sendMessage(QString)), controlPanel, SLOT(receiveMessage(QString)));
    QObject::connect(source, &Miniscope::newHeadQuaternion, commutator, &Commutator::handleNewQuaternion);

    commutatorThread = new QThread;
    commutator->moveToThread(commutatorThread);
    QObject::connect(commutatorThread, SIGNAL(started()), commutator, SLOT(startRunning()));
    commutatorThread->start();
}

