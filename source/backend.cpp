#include "backend.h"
#include <QDebug>
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
#include <QStandardItemModel>
#include <QStandardItem>
#include <QModelIndex>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "miniscope.h"
#include "behaviorcam.h"
#include "controlpanel.h"
#include "datasaver.h"
#include "behaviortracker.h"
#include "tracedisplay.h"

#ifdef USE_USB
 #include <libusb.h>
#endif

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

backEnd::backEnd(QObject *parent) :
    QObject(parent),
    m_versionNumber(""),
    m_buildInfo(""),
    m_userConfigFileName(""),
    m_userConfigOK(false),
    traceDisplay(nullptr),
    behavTracker(nullptr),
    m_jsonTreeModel(new QStandardItemModel())
{
#ifdef DEBUG
//    QString homePath = QDir::homePath();
    m_userConfigFileName = "./userConfigs/UserConfigExample.json";
//    loadUserConfigFile();
    handleUserConfigFileNameChanged();

//    setUserConfigOK(true);
#endif
    m_softwareStartTime = QDateTime().currentMSecsSinceEpoch();

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

#ifdef USE_USB
    testLibusb();
#endif

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
            //emit userConfigFileNameChanged();
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

void backEnd::constructJsonTreeModel()
{
    m_jsonTreeModel->clear();
    m_jsonTreeModel->setColumnCount(3);
    m_standardItem.clear();


    roles[Qt::UserRole + 1] = "key";
    roles[Qt::UserRole + 2] = "value";
    roles[Qt::UserRole + 3] = "type";
    roles[Qt::UserRole + 4] = "tips";

    m_jsonTreeModel->setItemRoleNames(roles);
//    qDebug() << "ROLE" << m_jsonTreeModel->roleNames();

//    QStandardItem *parentItem = m_jsonTreeModel->invisibleRootItem();

    QStringList keys = m_userConfig.keys();
    QString tempType;
    QString tempS;
    for (int i=0; i < keys.length(); i++) {
        if (!keys[i].contains("COMMENT")) {
            tempType = m_configProps[keys[i]].toObject()["type"].toString("String");
            tempS = m_userConfig[keys[i]].toString();
            if (tempType.contains("path") || tempType.contains("Path")) {

                tempS = tempS.replace("\\","/"); // Corrects the slashes
            }

            if(m_userConfig[keys[i]].isObject()) {
                m_standardItem.append(new QStandardItem());
                m_standardItem.last()->setData(keys[i], Qt::UserRole + 1);
                m_standardItem.last()->setData("", Qt::UserRole + 2);
                m_standardItem.last()->setData("Object", Qt::UserRole + 3);
    //            m_standardItem.append(handleJsonObject(m_standardItem.last(), m_userConfig[keys[i]].toObject()));
                m_jsonTreeModel->appendRow(handleJsonObject(m_standardItem.last(), m_userConfig[keys[i]].toObject(), m_configProps[keys[i]].toObject()));
            }
            else if (m_userConfig[keys[i]].isArray()) {
                m_standardItem.append(new QStandardItem());
                m_standardItem.last()->setData(keys[i], Qt::UserRole + 1);
                m_standardItem.last()->setData("", Qt::UserRole + 2);
                m_standardItem.last()->setData(m_configProps[keys[i]].toObject()["type"].toString(), Qt::UserRole + 3);
                m_standardItem.last()->setData(m_configProps[keys[i]].toObject()["tips"].toString(), Qt::UserRole + 4);
                m_jsonTreeModel->appendRow(handleJsonArray(m_standardItem.last(), m_userConfig[keys[i]].toArray(), m_configProps[keys[i]].toObject()["type"].toString()));

            }
            else if (m_userConfig[keys[i]].isString()){
                m_standardItem.append(new QStandardItem());
                m_standardItem.last()->setColumnCount(3);
                m_standardItem.last()->setData(keys[i], Qt::UserRole + 1);
                m_standardItem.last()->setData(tempS, Qt::UserRole + 2);
                m_standardItem.last()->setData(m_configProps[keys[i]].toObject()["type"].toString("String"), Qt::UserRole + 3);
                m_standardItem.last()->setData(m_configProps[keys[i]].toObject()["tips"].toString(), Qt::UserRole + 4);
                m_jsonTreeModel->appendRow(m_standardItem.last());
            }
            else if (m_userConfig[keys[i]].isBool()) {
                m_standardItem.append(new QStandardItem());
                m_standardItem.last()->setColumnCount(3);
                m_standardItem.last()->setData(keys[i],Qt::UserRole + 1);
                m_standardItem.last()->setData(m_userConfig[keys[i]].toBool(),Qt::UserRole + 2);
                m_standardItem.last()->setData(m_configProps[keys[i]].toObject()["type"].toString("Bool"),Qt::UserRole + 3);
                m_standardItem.last()->setData(m_configProps[keys[i]].toObject()["tips"].toString(), Qt::UserRole + 4);
                m_jsonTreeModel->appendRow(m_standardItem.last());
            }
            else if (m_userConfig[keys[i]].isDouble()) {
                m_standardItem.append(new QStandardItem());
                m_standardItem.last()->setColumnCount(3);
                m_standardItem.last()->setData(keys[i],Qt::UserRole + 1);
                m_standardItem.last()->setData(m_userConfig[keys[i]].toDouble(),Qt::UserRole + 2);
                m_standardItem.last()->setData(m_configProps[keys[i]].toObject()["type"].toString("Double"),Qt::UserRole + 3);
                m_standardItem.last()->setData(m_configProps[keys[i]].toObject()["tips"].toString(), Qt::UserRole + 4);
                m_jsonTreeModel->appendRow(m_standardItem.last());
            }
        }
    }
}

void backEnd::treeViewTextChanged(const QModelIndex &index, QString text)
{
    if (index.isValid()) {

        QStandardItem *item = m_jsonTreeModel->itemFromIndex(index);

        if (item->data(Qt::UserRole + 3).toString().contains("path") || item->data(Qt::UserRole + 3).toString().contains("Path") ) {
            text = text.replace("\\","/");
        }

        item->setData(text,Qt::UserRole + 2);
//        qDebug() << "TEXT!"  << item->data(Qt::UserRole + 1) << item->data(Qt::UserRole + 2) << text;
    }
}

QStandardItem *backEnd::handleJsonObject(QStandardItem *parent, QJsonObject obj, QJsonObject objProps)
{
    QStringList keys = obj.keys();

    QString tempType;
    QString tempS;

    for (int i=0; i < keys.length(); i++) {
        if (!keys[i].contains("COMMENT")) {

            tempType = objProps[keys[i]].toObject()["type"].toString();
            tempS = obj[keys[i]].toString();
            if (tempType.contains("path") || tempType.contains("Path")) {
                tempS = tempS.replace("\\","/"); // Corrects the slashes
            }

            if(obj[keys[i]].isObject()) {
                m_standardItem.append(new QStandardItem());
                m_standardItem.last()->setData(keys[i], Qt::UserRole + 1);
                m_standardItem.last()->setData("", Qt::UserRole + 2);
                m_standardItem.last()->setData("Object", Qt::UserRole + 3);

                if (parent->data(Qt::UserRole + 1).toString() == "cameras")
                    parent->appendRow(handleJsonObject(m_standardItem.last(), obj[keys[i]].toObject(), objProps["cameraDeviceName"].toObject()));
                else if (parent->data(Qt::UserRole + 1).toString() == "miniscopes")
                    parent->appendRow(handleJsonObject(m_standardItem.last(), obj[keys[i]].toObject(), objProps["miniscopeDeviceName"].toObject()));
                else
                    parent->appendRow(handleJsonObject(m_standardItem.last(), obj[keys[i]].toObject(), objProps[keys[i]].toObject()));

            }
            else if (obj[keys[i]].isArray()) {
                m_standardItem.append(new QStandardItem());
                m_standardItem.last()->setData(keys[i], Qt::UserRole + 1);
                m_standardItem.last()->setData("", Qt::UserRole + 2);
                m_standardItem.last()->setData(objProps[keys[i]].toObject()["type"].toString(), Qt::UserRole + 3);
                m_standardItem.last()->setData(objProps[keys[i]].toObject()["tips"].toString(), Qt::UserRole + 4);
                parent->appendRow(handleJsonArray(m_standardItem.last(), obj[keys[i]].toArray(), objProps[keys[i]].toObject()["type"].toString()));

            }
            else if (obj[keys[i]].isString()){
                m_standardItem.append(new QStandardItem());
    //            m_standardItem.last()->setColumnCount(3);
                m_standardItem.last()->setData(keys[i], Qt::UserRole + 1);
                m_standardItem.last()->setData(tempS, Qt::UserRole + 2);
                m_standardItem.last()->setData(objProps[keys[i]].toObject()["type"].toString("String"), Qt::UserRole + 3);
                m_standardItem.last()->setData(objProps[keys[i]].toObject()["tips"].toString(), Qt::UserRole + 4);
                parent->appendRow(m_standardItem.last());
            }
            else if (obj[keys[i]].isBool()) {
                m_standardItem.append(new QStandardItem());
    //            m_standardItem.last()->setColumnCount(3);
                m_standardItem.last()->setData(keys[i],Qt::UserRole + 1);
                m_standardItem.last()->setData(obj[keys[i]].toBool(),Qt::UserRole + 2);
                m_standardItem.last()->setData(objProps[keys[i]].toObject()["type"].toString("Bool"),Qt::UserRole + 3);
                m_standardItem.last()->setData(objProps[keys[i]].toObject()["tips"].toString(), Qt::UserRole + 4);
                parent->appendRow(m_standardItem.last());
            }
            else if (obj[keys[i]].isDouble()) {
                m_standardItem.append(new QStandardItem());
    //            m_standardItem.last()->setColumnCount(3);
                m_standardItem.last()->setData(keys[i],Qt::UserRole + 1);
                m_standardItem.last()->setData(obj[keys[i]].toDouble(),Qt::UserRole + 2);
                m_standardItem.last()->setData(objProps[keys[i]].toObject()["type"].toString("Double"),Qt::UserRole + 3);
                m_standardItem.last()->setData(objProps[keys[i]].toObject()["tips"].toString(), Qt::UserRole + 4);
                parent->appendRow(m_standardItem.last());
            }
        }
    }
    return parent;
}

QStandardItem *backEnd::handleJsonArray(QStandardItem *parent, QJsonArray arry, QString type)
{
//    QStringList keys = obj.keys();
//    type = type.right(6);
    type = type.right(type.length() - 6);
    type = type.chopped(1);
    qDebug() << "TYPE" << type;
    if (type != "String" && type != "Bool" && type != "Integer" && type != "Double" && type != "Number" && type != "Object" && type.left(5) != "Array") {
        qDebug() << "TYPE" << type;
        type = "String";
    }

    for (int i=0; i < arry.size(); i++) {
        if(arry[i].isObject()) {
            m_standardItem.append(new QStandardItem());
            m_standardItem.last()->setData("Object", Qt::UserRole + 1);
            m_standardItem.last()->setData("", Qt::UserRole + 2);
            m_standardItem.last()->setData("Object", Qt::UserRole + 3);
            parent->appendRow(handleJsonObject(m_standardItem.last(), arry[i].toObject(), QJsonObject()));
        }
        else if (arry[i].isArray()) {
            m_standardItem.append(new QStandardItem());
            m_standardItem.last()->setData("Array", Qt::UserRole + 1);
            m_standardItem.last()->setData("", Qt::UserRole + 2);
            m_standardItem.last()->setData(type, Qt::UserRole + 3);
            parent->appendRow(handleJsonArray(m_standardItem.last(), arry[i].toArray(), type));

        }
        else if (arry[i].isString()){
            m_standardItem.append(new QStandardItem());
//            m_standardItem.last()->setColumnCount(3);
            m_standardItem.last()->setData("", Qt::UserRole + 1);
            m_standardItem.last()->setData(arry[i].toString(),Qt::UserRole + 2);
            m_standardItem.last()->setData(type, Qt::UserRole + 3);
            parent->appendRow(m_standardItem.last());
        }
        else if (arry[i].isBool()) {
            m_standardItem.append(new QStandardItem());
//            m_standardItem.last()->setColumnCount(3);
            m_standardItem.last()->setData("",Qt::UserRole + 1);
            m_standardItem.last()->setData(arry[i].toBool(),Qt::UserRole + 2);
            m_standardItem.last()->setData(type,Qt::UserRole + 3);
            parent->appendRow(m_standardItem.last());
        }
        else if (arry[i].isDouble()) {
            m_standardItem.append(new QStandardItem());
//            m_standardItem.last()->setColumnCount(3);
            m_standardItem.last()->setData("",Qt::UserRole + 1);
            m_standardItem.last()->setData(arry[i].toDouble(),Qt::UserRole + 2);
            m_standardItem.last()->setData(type,Qt::UserRole + 3);
            parent->appendRow(m_standardItem.last());
        }
    }
    return parent;
}

void backEnd::generateUserConfigFromModel()
{

//    void forEach(QAbstractItemModel* model, QModelIndex parent = QModelIndex()) {
//        for(int r = 0; r < model->rowCount(parent); ++r) {
//            QModelIndex index = model->index(r, 0, parent);
//            QVariant name = model->data(index);
//            qDebug() << name;
//            // here is your applicable code
//            if( model->hasChildren(index) ) {
//                forEach(model, index);
//            }
//        }
//    }

    QJsonObject jConfig;
    QString key, value, type;
    for (int i=0; i < m_jsonTreeModel->rowCount(); i++) {
        QModelIndex index = m_jsonTreeModel->index(i, 0);
        key = m_jsonTreeModel->data(index, Qt::UserRole + 1).toString();
        value = m_jsonTreeModel->data(index, Qt::UserRole + 2).toString();
        type = m_jsonTreeModel->data(index, Qt::UserRole + 3).toString();
        if (type == "Object") {
            jConfig[key] = getObjectFromModel(index);
        }
        else if (type.left(5) == "Array") {
             jConfig[key] = getArrayFromModel(index);
        }
        else if (type == "String" || type == "DirPath" || type == "FilePath" || type == "CameraDeviceType" || type == "MiniscopeDeviceType") {
            jConfig[key] = value;
        }
        else if (type == "Bool") {
            if (value == "true")
                jConfig[key] = true;
            else if (value == "false")
                jConfig[key] = false;
        }
        else if (type == "Number" || type == "Integer" || type == "Double") {
            jConfig[key] = value.toDouble();
        }
    }

    m_userConfig = jConfig;
//    QJsonDocument d;
//    d.setObject(jConfig);
//    QFile file;
//    file.setFileName("JSONNNNNNN.json");
//    file.open(QFile::WriteOnly | QFile::Text | QFile::Truncate);
//    file.write(d.toJson());
//    file.close();
}

QJsonObject backEnd::getObjectFromModel(QModelIndex idx)
{
    QJsonObject jObj;

    QString key, value, type;

    for (int i=0; i < m_jsonTreeModel->rowCount(idx); i++) {
        QModelIndex index = m_jsonTreeModel->index(i, 0, idx);
        key = m_jsonTreeModel->data(index, Qt::UserRole + 1).toString();
        value = m_jsonTreeModel->data(index, Qt::UserRole + 2).toString();
        type = m_jsonTreeModel->data(index, Qt::UserRole + 3).toString();

        if (type == "Object") {
            jObj[key] = getObjectFromModel(index);
        }
        else if (type.left(5) == "Array") {
             jObj[key] = getArrayFromModel(index);
        }
        else if (type == "String" || type == "DirPath" || type == "FilePath" || type == "CameraDeviceType" || type == "MiniscopeDeviceType") {
            jObj[key] = value;
        }
        else if (type == "Bool") {
            if (value == "true")
                jObj[key] = true;
            else if (value == "false")
                jObj[key] = false;
        }
        else if (type == "Number" || type == "Integer" || type == "Double") {
            jObj[key] = value.toDouble();
        }

    }

    return jObj;
}

QJsonArray backEnd::getArrayFromModel(QModelIndex idx)
{
    QJsonArray jAry;

    QString key, value, type;

    for (int i=0; i < m_jsonTreeModel->rowCount(idx); i++) {
        QModelIndex index = m_jsonTreeModel->index(i, 0, idx);
        key = m_jsonTreeModel->data(index, Qt::UserRole + 1).toString();
        value = m_jsonTreeModel->data(index, Qt::UserRole + 2).toString();
        type = m_jsonTreeModel->data(index, Qt::UserRole + 3).toString();
        if (type == "Object") {
            jAry.append(getObjectFromModel(index));
        }
        else if (type.left(5) == "Array") {
             jAry.append(getArrayFromModel(index));
        }
        else if (type == "String" || type == "DirPath" || type == "FilePath" || type == "CameraDeviceType" || type == "MiniscopeDeviceType") {
            qDebug() << "STRRRRIIINNNGGG" << value;
            jAry.append(value);
        }
        else if (type == "Bool") {
            if (value == "true")
                jAry.append(true);
            else if (value == "false")
                jAry.append(false);
        }
        else if (type == "Number" || type == "Integer" || type == "Double") {
            jAry.append(value.toDouble());
        }

    }

    return jAry;
}

void backEnd::saveConfigObject()
{
    generateUserConfigFromModel();
    QJsonDocument d;
    d.setObject(m_userConfig);
    QFile file;
    QString fName = m_userConfigFileName;
    fName.chop(5);
    fName.append("_new.json");
    file.setFileName(fName);
    file.open(QFile::WriteOnly | QFile::Text | QFile::Truncate);
    file.write(d.toJson());
    file.close();
}

void backEnd::saveConfigObjectAs(const QString &filePath)
{
    generateUserConfigFromModel();
    QJsonDocument d;
    d.setObject(m_userConfig);
    QFile file(filePath);
    if (file.open(QFile::WriteOnly | QFile::Text | QFile::Truncate)) {
        file.write(d.toJson());
        file.close();
        sendMessage("User config saved to " + filePath);
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
// from the device catalog (deviceConfigs/videoDevices.json). Both then rebuild the
// tree model and re-run the validity check (which enables Save / Run). All of the
// existing editor, serialization and save machinery is reused unchanged.
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
        QJsonObject td = cfg["traceDisplay"].toObject();
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
        QJsonObject bt = cfg["behaviorTracker"].toObject();
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

    constructJsonTreeModel();
    updateHasDevices();
    checkUserConfigForIssues();     // emits userConfigOKChanged() -> enables Save/Run
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

    // Capture any edits already made in the tree before we rebuild it.
    generateUserConfigFromModel();

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

    constructJsonTreeModel();
    updateHasDevices();
    checkUserConfigForIssues();
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
#else
    return QStringLiteral("Device scan is not available on this platform.");
#endif
}

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
    const QStringList names = enumerateVideoDevices();
    if (names.isEmpty())
        return QStringLiteral("No video devices detected.");
    QStringList lines;
    for (int i = 0; i < names.size(); i++)
        lines << QString("    deviceID %1:  %2")
                     .arg(i)
                     .arg(names[i].isEmpty() ? QStringLiteral("(unknown)") : names[i]);
    return QStringLiteral("Detected video devices:\n") + lines.join("\n")
           + QStringLiteral("\n\nUse these deviceID numbers in your user config. "
                            "Note: a Miniscope might appear under a generic name "
                            "(e.g. \"USB Video Device\").");
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
#endif

QStringList backEnd::availableDeviceIDs()
{
    // Capture any edits made in the tree so the used-ID set reflects the live config.
    generateUserConfigFromModel();

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
#ifdef Q_OS_WINDOWS
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
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    jsonFile = file.readAll();
    setUserConfigDisplay("User Config File Selected: " + m_userConfigFileName + "\n" + jsonFile);
    file.close();
    QJsonDocument d = QJsonDocument::fromJson(jsonFile.toUtf8());
    m_userConfig = d.object();

    // Correct for old device structure in user config files
    QJsonObject tempObj;
    QJsonObject deviceObj = m_userConfig["devices"].toObject();
    if (m_userConfig["devices"].toObject()["miniscopes"].isArray()) {
        QJsonArray tempAry = m_userConfig["devices"].toObject()["miniscopes"].toArray();
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
    if (m_userConfig["devices"].toObject()["cameras"].isArray()) {
        QJsonArray tempAry = m_userConfig["devices"].toObject()["cameras"].toArray();
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
}

void backEnd::onRunClicked()
{
//    qDebug() << "Run was clicked!";
    generateUserConfigFromModel();
    parseUserConfig();
    updateHasDevices();
    checkUserConfigForIssues();
    if (m_userConfigOK) {

        constructUserConfigGUI();

        setupDataSaver(); // must happen after devices have been made
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
    // TODO: Do other exit stuff such as stop recording???
    emit closeAll();

}

void backEnd::handleUserConfigFileNameChanged()
{
    loadUserConfigFile();
    constructJsonTreeModel();
    parseUserConfig();
    updateHasDevices();
    checkUserConfigForIssues();
}

void backEnd::connectSnS()
{

    // Start and stop recording signals
    QObject::connect(controlPanel, SIGNAL( recordStart(QMap<QString,QVariant>)), dataSaver, SLOT (startRecording(QMap<QString,QVariant>)));
    QObject::connect(controlPanel, SIGNAL( recordStop()), dataSaver, SLOT (stopRecording()));
    QObject::connect((controlPanel), SIGNAL( sendNote(QString) ), dataSaver, SLOT ( takeNote(QString) ));
    QObject::connect(this, SIGNAL( closeAll()), controlPanel, SLOT (close()));

    // Trace window is optional; only tear it down on exit if it was created.
    if (traceDisplay)
        QObject::connect(this, SIGNAL( closeAll()), traceDisplay, SLOT (close()));

    QObject::connect(dataSaver, SIGNAL(sendMessage(QString)), controlPanel, SLOT( receiveMessage(QString)));

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
                                            nullptr,
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
    // TODO: make return do something or remove
    return true;
}

void backEnd::parseUserConfig()
{
    QJsonObject devices = m_userConfig["devices"].toObject();
    QJsonArray tempArray;
    QJsonObject tempObj;
    QStringList s;
    int count = 0;

    // Main JSON header
    researcherName = m_userConfig["researcherName"].toString();
    dataDirectory= m_userConfig["dataDirectory"].toString();
    dataStructureOrder = m_userConfig["dataStructureOrder"].toArray();
    experimentName = m_userConfig["experimentName"].toString();
    animalName = m_userConfig["animalName"].toString();

    // JSON subsections
    ucExperiment = m_userConfig["experiment"].toObject();

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
            qDebug() << "DNSOSNDAIOASDNO" << tempObj;
            ucBehaviorCams[s[i]] = tempObj;
        }

    }

//    ucBehaviorCams = devices["cameras"].toArray();

    ucBehaviorTracker = m_userConfig["behaviorTracker"].toObject();
    ucTraceDisplay = m_userConfig["traceDisplay"].toObject();


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

    // Load main control GUI
    controlPanel = new ControlPanel(this, m_userConfig);
    QObject::connect(this, SIGNAL (sendMessage(QString) ), controlPanel, SLOT( receiveMessage(QString)));

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

    connectSnS();
}

void backEnd::testLibusb() {
#ifdef USE_USB
    int mcu_ep_out = 4;
    int mcu_ep_in  = 3;
    int mcu_device_id = 1003;
    int mcu_product_id = 9251;

    uint8_t data[1024];
    uint8_t inData[1024];
    for (int k=0 ; k < 1024; k++) {
        data[k] = k&0xFF;
        inData[k] = 0;
    }

    int actualLength = 0;

    struct libusb_device_descriptor desc;

    const struct libusb_endpoint_descriptor *epdesc;
    const struct libusb_interface_descriptor *interdesc;
    libusb_device_handle *d_h = NULL;
    libusb_device *dev;

    // Notes:
    // When using cypress:
    // Interface 2 with 0x04 endpoint for bulk seems to work.



    libusb_device **devs;
    int r;
    ssize_t cnt;

    r = libusb_init(NULL);
//    libusb_set_debug(NULL, 4);
    if (r < 0)
        qDebug() << "Issue with libusb_init.";
    else {
        cnt = libusb_get_device_list(NULL, &devs);
        if (cnt < 0){
            libusb_exit(NULL);
            qDebug() << "Issue with libusb_get_device_list.";
        }
        else {

            int i = 0, j = 0;
            uint8_t path[8];

            while ((dev = devs[i++]) != NULL) {
                // Loop through USB devices

                int r = libusb_get_device_descriptor(dev, &desc);
                if (r < 0) {
                    qDebug() << "Failed to get device descriptor.";
                }
                else if (desc.idVendor == mcu_device_id && desc.idProduct == mcu_product_id){

                    qDebug() << "VendorID: " << desc.idVendor << " | ProductID: " << desc.idProduct << " | Bus Number:" << libusb_get_bus_number(dev) << " | Device Address:" << libusb_get_device_address(dev);

                    r = libusb_get_port_numbers(dev, path, sizeof(path));
                    if (r > 0) {
                        qDebug() <<"Paths:";
                        for (j = 0; j < r; j++)
                            qDebug() << "    " << j << ": " << path[j];
                    }



                    struct libusb_config_descriptor **configDesc;
                    r = libusb_get_active_config_descriptor(dev,configDesc);

                    if (r < 0)
                        qDebug() << "Config Desc failed:" << r;
                    else {
                        int numAltSettings = configDesc[0]->interface->num_altsetting;
                        qDebug() << "Number of alt settings: " << numAltSettings;
                        interdesc = configDesc[0]->interface->altsetting;
                        for (int w=0; w < numAltSettings; w++) {
                            qDebug() << "Number of endpoints: "<< interdesc[w].bNumEndpoints;
                            for(int m=0; m < interdesc[w].bNumEndpoints; m++) {
                                epdesc = &interdesc[w].endpoint[m];
                                qDebug()<<"Descriptor Type: "<<(int)epdesc->bDescriptorType;
                                qDebug()<<"Attributes Type: "<<(int)epdesc->bmAttributes;
                                qDebug()<<"EP Address: "<<(int)epdesc->bEndpointAddress;
                            }
                        }
                        qDebug() << "HERE~!!";
                        libusb_free_config_descriptor(*configDesc);
                    }

                    r = libusb_open(dev,&d_h);

                    if (r < 0) {
                        qDebug() << "libusb_open failed. Error code is: " << r;
                    }
                    else {
                        unsigned char name[200];
                        r = libusb_get_string_descriptor_ascii(d_h, desc.iProduct, name, 200);
                        if ( r > 0) {
                            qDebug() << "iProduct Name: " << QString::fromUtf8((char *)name, r);
                        }

                        // For Cypress Bulk, use interface 2 and endpoint 0x04|LIBUSB_ENDPOINT_ IN/OUT


                        // I think windows is already setting up configuration 1 so an error in setting config is ok to skip???
                        int conf;
                        libusb_get_configuration(d_h,&conf);
                        qDebug() << "Get Config: " << conf;
                        r = libusb_set_configuration(d_h,1);
                        if (r < 0)
                            qDebug() << "set configuration issue: " << libusb_error_name(r);
                         if (1){
                            r = libusb_claim_interface(d_h,0);

                            if (r < 0)
                                qDebug() << "claim interface issue: " << r;
                            else {
                                r = libusb_set_interface_alt_setting(d_h,0,1);
                                if (r < 0)
                                    qDebug() << "set alt interface issue: " << r;
                                else {

                                    qDebug() <<  "Control Sending" << data[0] << data[1] << data[2] << data[3] << data[4];
                                    r = libusb_control_transfer(d_h,LIBUSB_REQUEST_TYPE_VENDOR|LIBUSB_RECIPIENT_INTERFACE,0,0,0,data,sizeof(data),1000);
//                                    r = libusb_interrupt_transfer(d_h,0x02,data,1024,NULL,1000);
//                                    r = libusb_bulk_transfer(d_h, mcu_ep_out|LIBUSB_ENDPOINT_OUT, data, 1024, NULL, 1000);
                                    if (r < 0)
                                        qDebug() << "Issue sending bulk transfer to device:" << r;
                                    if(1) {
                                        r = libusb_control_transfer(d_h,LIBUSB_REQUEST_TYPE_VENDOR|LIBUSB_RECIPIENT_INTERFACE|LIBUSB_ENDPOINT_IN,0,0,0,inData,sizeof(inData),1000);
//                                        r = libusb_interrupt_transfer(d_h, 0x01|LIBUSB_ENDPOINT_IN, inData, 1024, &actualLength, 1000);
//                                        r = libusb_bulk_transfer(d_h, mcu_ep_in|LIBUSB_ENDPOINT_IN, inData, 1024, &actualLength, 1000);
                                        if (r < 0) {
                                            qDebug() << "Receiving issue: " << libusb_error_name(r);
                                        }
                                        else
                                            qDebug() << "Receiving" << inData[0] << inData[1] << inData[2] << inData[3] << inData[4];
                                    }


                                    for (int k=0 ; k < 1024; k++) {
                                        data[k] = k&0xFF;
                                        inData[k] = 0;
                                    }

//
                                    actualLength = 1024;
                                    r = libusb_interrupt_transfer(d_h, 0x02, data, sizeof(data), NULL, 1000);
                                    qDebug() <<  "Interrupt Sending" << data[0] << data[1] << data[2] << data[3] << data[4];
                                    if (r < 0)
                                        qDebug() << "Issue sending interrupt transfer to device:" << r;
                                    if (1) {
                                        actualLength = 0;
//                                        r = libusb_control_transfer(d_h,LIBUSB_REQUEST_TYPE_VENDOR|LIBUSB_RECIPIENT_INTERFACE|LIBUSB_ENDPOINT_IN,0,0,0,inData,sizeof(inData),1000);
                                        r = libusb_interrupt_transfer(d_h, 0x01|LIBUSB_ENDPOINT_IN, inData, sizeof(inData), &actualLength, 1000);
//                                        r = libusb_bulk_transfer(d_h, mcu_ep_in|LIBUSB_ENDPOINT_IN, inData, 1024, &actualLength, 1000);
                                        if (r < 0) {
                                            qDebug() << "Receiving interrupt issue: " << libusb_error_name(r);
                                            qDebug() << "interrupt Receiving" << inData[0] << inData[1] << inData[2] << inData[3] << inData[4] << "inLength:" << actualLength;
                                        }
                                        else
                                            qDebug() << "interrupt Receiving" << inData[0] << inData[1] << inData[2] << inData[3] << inData[4] << "inLength:" << actualLength;
                                    }



                                    qDebug() <<  "Bulk Sending" << data[0] << data[1] << data[2] << data[3] << data[4];
//                                    r = libusb_control_transfer(d_h,LIBUSB_REQUEST_TYPE_VENDOR|LIBUSB_RECIPIENT_INTERFACE,0,0,0,data,sizeof(data),1000);
//                                    r = libusb_interrupt_transfer(d_h,0x02,data,1024,NULL,1000);
                                    r = libusb_bulk_transfer(d_h, 0x04, data, 1024, NULL, 1000);
                                    if (r < 0)
                                        qDebug() << "Issue sending bulk transfer to device:" << r;
                                    if (1) {
//                                        r = libusb_control_transfer(d_h,LIBUSB_REQUEST_TYPE_VENDOR|LIBUSB_RECIPIENT_INTERFACE|LIBUSB_ENDPOINT_IN,0,0,0,inData,sizeof(inData),1000);
//                                        r = libusb_interrupt_transfer(d_h, 0x01|LIBUSB_ENDPOINT_IN, inData, 1024, &actualLength, 1000);
                                        r = libusb_bulk_transfer(d_h, 0x83, inData, 1024, &actualLength, 1000);
                                        if (r < 0) {
                                            qDebug() << "Bulk Receiving issue: " << libusb_error_name(r);
                                            qDebug() << "Bulk Receiving" << inData[0] << inData[1] << inData[2] << inData[3] << inData[4] << "inLength:" << actualLength;
                                        }
                                        else
                                            qDebug() << "Bulk Receiving" << inData[0] << inData[1] << inData[2] << inData[3] << inData[4] << "inLength:" << actualLength;
                                    }
                                }
                            }
                            libusb_release_interface(d_h, 0);
                        }

                    }
                    libusb_close(d_h);
                }
                qDebug() << " ";
            }
            // ---------------

            libusb_free_device_list(devs, 1);
            libusb_exit(NULL);
        }
    }
#endif
}
