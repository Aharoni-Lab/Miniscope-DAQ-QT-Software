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

backEnd::backEnd(QObject *parent) :
    QObject(parent),
    m_versionNumber(""),
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
        // ---- LIBUSB TEST ----
    libusb_device **devs;
    int r;
    ssize_t cnt;

    r = libusb_init(NULL);
    if (r < 0)
        qDebug() << "Problem 1 ";
    else {
        cnt = libusb_get_device_list(NULL, &devs);
        if (cnt < 0){
            libusb_exit(NULL);
            qDebug() << "Problem 2";
        }
        else {
            // -----------
            libusb_device *dev;
            int i = 0, j = 0;
            uint8_t path[8];

            while ((dev = devs[i++]) != NULL) {
                struct libusb_device_descriptor desc;
                struct libusb_config_descriptor **configDesc;
                const struct libusb_endpoint_descriptor *epdesc;
                const struct libusb_interface_descriptor *interdesc;
                int r = libusb_get_device_descriptor(dev, &desc);
                if (r < 0) {
                    qDebug() << "failed to get device descriptor";
                }
                else {

                    qDebug() << desc.idVendor << ":" << desc.idProduct << "bus" << libusb_get_bus_number(dev) << "device" << libusb_get_device_address(dev);

                    r = libusb_get_port_numbers(dev, path, sizeof(path));
                    if (r > 0) {
                        qDebug() <<"path:" << path[0];
                        for (j = 1; j < r; j++)
                            qDebug() << " more paths:" << path[j];
                    }




                    libusb_device_handle *d_h = NULL;
                    r = libusb_open(dev,&d_h);
                    if ( r == 0) {
                        unsigned char name[200];
                        r = libusb_get_string_descriptor_ascii(d_h,desc.iProduct,name,200);
                        if ( r > 0) {
                            qDebug() << "name" << QString::fromUtf8((char *)name, r);
                        }
 //                        libusb_set_configuration(d_h,0);
 //                        r = libusb_get_active_config_descriptor(dev,configDesc);
 //                        if (r < 0)
 //                            qDebug() << "Config Desc failed:" << r;
 //                        else {
 //                            qDebug() << "Number of alt settings:" << configDesc[0]->interface->num_altsetting;
 //                            interdesc = configDesc[0]->interface->altsetting;

 //                                qDebug() << "Number of endpoints: "<< interdesc->bNumEndpoints;
 //                                for(int k=0; k<(int)interdesc->bNumEndpoints; k++) {
 //                                    epdesc = &interdesc->endpoint[k];
 //                                    qDebug()<<"Descriptor Type: "<<(int)epdesc->bDescriptorType;
 //                                    qDebug()<<"EP Address: "<<(int)epdesc->bEndpointAddress;
 //                                }
 //                            }
 // //                        }
 //                        libusb_free_config_descriptor(configDesc[0]);

                        libusb_claim_interface(d_h,2);
                        uint8_t data[5] = {0,1,2,3,4};
                        uint8_t inData[1024];
                        int actualLength;
                        qDebug() << "Sending" << data[0] << data[1] << data[2] << data[3] << data[4];
                        r = libusb_bulk_transfer(d_h,0x04|LIBUSB_ENDPOINT_OUT,data,5,NULL,1000);
                        if (r != 0)
                            qDebug() << "Issue sending bulk transfer to device:" << r;
                        else {
                            libusb_bulk_transfer(d_h,0x04|LIBUSB_ENDPOINT_IN ,inData,1024,&actualLength,1000);
                            qDebug() << "Receiving" << inData[0] << inData[1] << inData[2] << inData[3] << inData[4] << "inLength:" << actualLength;
                        }
                        libusb_close(d_h);
                    }
                    else {
                        qDebug() << "Open Fail:" << r;
                    }
                }
            }
            // ---------------

            libusb_free_device_list(devs, 1);
            libusb_exit(NULL);
        }
    }
#endif
    testCodecSupport();
    QString tempStr;
    for (int i = 0; i < m_availableCodec.length(); i++)
        m_availableCodecList += m_availableCodec[i] + ", ";

    m_availableCodecList = m_availableCodecList.chopped(2);
    for (int i = 0; i < unAvailableCodec.length(); i++)
        tempStr += unAvailableCodec[i] + ", ";

    setUserConfigDisplay("Select a User Configuration file.\n\nSupported devices are listed in the .json files in the deviceConfig folder.\n\nAvailable compression Codecs on your computer are:\n" + m_availableCodecList +
                         "\n\nUnavailable compression Codes on your computer are:\n" + tempStr.chopped(2));

//    QObject::connect(this, SIGNAL (userConfigFileNameChanged()), this, SLOT( handleUserConfigFileNameChanged() ));

    QString jsonFile;
    QFile file;
    file.setFileName("deviceConfigs/userConfigProps.json");
    bool status = file.open(QIODevice::ReadOnly | QIODevice::Text);
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

    m_jsonTreeModel->setItemRoleNames(roles);
//    qDebug() << "ROLE" << m_jsonTreeModel->roleNames();

//    QStandardItem *parentItem = m_jsonTreeModel->invisibleRootItem();

    QStringList keys = m_userConfig.keys();
    for (int i=0; i < keys.length(); i++) {
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
            m_jsonTreeModel->appendRow(handleJsonArray(m_standardItem.last(), m_userConfig[keys[i]].toArray(), m_configProps[keys[i]].toObject()["type"].toString()));

        }
        else if (m_userConfig[keys[i]].isString()){
            m_standardItem.append(new QStandardItem());
            m_standardItem.last()->setColumnCount(3);
            m_standardItem.last()->setData(keys[i], Qt::UserRole + 1);
            m_standardItem.last()->setData(m_userConfig[keys[i]].toString(),Qt::UserRole + 2);
            m_standardItem.last()->setData(m_configProps[keys[i]].toObject()["type"].toString("String"), Qt::UserRole + 3);
            m_jsonTreeModel->appendRow(m_standardItem.last());
        }
        else if (m_userConfig[keys[i]].isBool()) {
            m_standardItem.append(new QStandardItem());
            m_standardItem.last()->setColumnCount(3);
            m_standardItem.last()->setData(keys[i],Qt::UserRole + 1);
            m_standardItem.last()->setData(m_userConfig[keys[i]].toBool(),Qt::UserRole + 2);
            m_standardItem.last()->setData(m_configProps[keys[i]].toObject()["type"].toString("Bool"),Qt::UserRole + 3);
            m_jsonTreeModel->appendRow(m_standardItem.last());
        }
        else if (m_userConfig[keys[i]].isDouble()) {
            m_standardItem.append(new QStandardItem());
            m_standardItem.last()->setColumnCount(3);
            m_standardItem.last()->setData(keys[i],Qt::UserRole + 1);
            m_standardItem.last()->setData(m_userConfig[keys[i]].toDouble(),Qt::UserRole + 2);
            m_standardItem.last()->setData(m_configProps[keys[i]].toObject()["type"].toString("Double"),Qt::UserRole + 3);
            m_jsonTreeModel->appendRow(m_standardItem.last());
        }
    }
}

void backEnd::treeViewTextChanged(const QModelIndex &index, QString text)
{
    if (index.isValid()) {
        QStandardItem *item = m_jsonTreeModel->itemFromIndex(index);
        item->setData(text,Qt::UserRole + 2);
        qDebug() << "TEXT!"  << item->data(Qt::UserRole + 1) << item->data(Qt::UserRole + 2) << text;
    }
}

QStandardItem *backEnd::handleJsonObject(QStandardItem *parent, QJsonObject obj, QJsonObject objProps)
{
    QStringList keys = obj.keys();
    for (int i=0; i < keys.length(); i++) {
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
            parent->appendRow(handleJsonArray(m_standardItem.last(), obj[keys[i]].toArray(), objProps[keys[i]].toObject()["type"].toString()));

        }
        else if (obj[keys[i]].isString()){
            m_standardItem.append(new QStandardItem());
//            m_standardItem.last()->setColumnCount(3);
            m_standardItem.last()->setData(keys[i], Qt::UserRole + 1);
            m_standardItem.last()->setData(obj[keys[i]].toString(),Qt::UserRole + 2);
            m_standardItem.last()->setData(objProps[keys[i]].toObject()["type"].toString(), Qt::UserRole + 3);
            parent->appendRow(m_standardItem.last());
        }
        else if (obj[keys[i]].isBool()) {
            m_standardItem.append(new QStandardItem());
//            m_standardItem.last()->setColumnCount(3);
            m_standardItem.last()->setData(keys[i],Qt::UserRole + 1);
            m_standardItem.last()->setData(obj[keys[i]].toBool(),Qt::UserRole + 2);
            m_standardItem.last()->setData(objProps[keys[i]].toObject()["type"].toString(),Qt::UserRole + 3);
            parent->appendRow(m_standardItem.last());
        }
        else if (obj[keys[i]].isDouble()) {
            m_standardItem.append(new QStandardItem());
//            m_standardItem.last()->setColumnCount(3);
            m_standardItem.last()->setData(keys[i],Qt::UserRole + 1);
            m_standardItem.last()->setData(obj[keys[i]].toDouble(),Qt::UserRole + 2);
            m_standardItem.last()->setData(objProps[keys[i]].toObject()["type"].toString(),Qt::UserRole + 3);
            parent->appendRow(m_standardItem.last());
        }
    }
    return parent;
}

QStandardItem *backEnd::handleJsonArray(QStandardItem *parent, QJsonArray arry, QString type)
{
//    QStringList keys = obj.keys();
//    type = type.right(6);
    type = type.right(type.length() - 7);
    type = type.chopped(1);
    for (int i=0; i < arry.size(); i++) {
        if(arry[i].isObject()) {
            m_standardItem.append(new QStandardItem());
            m_standardItem.last()->setData("Object", Qt::UserRole + 1);
            m_standardItem.last()->setData("", Qt::UserRole + 2);
            m_standardItem.last()->setData(type, Qt::UserRole + 3);
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

void backEnd::loadUserConfigFile()
{
    QString jsonFile;
    QFile file;
    file.setFileName(m_userConfigFileName);
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    jsonFile = file.readAll();
    setUserConfigDisplay("User Config File Selected: " + m_userConfigFileName + "\n" + jsonFile);
    file.close();
    QJsonDocument d = QJsonDocument::fromJson(jsonFile.toUtf8());
    m_userConfig = d.object();
}

void backEnd::onRunClicked()
{
//    qDebug() << "Run was clicked!";
    generateUserConfigFromModel();
    parseUserConfig();
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
    checkUserConfigForIssues();
}

void backEnd::connectSnS()
{

    // Start and stop recording signals
    QObject::connect(controlPanel, SIGNAL( recordStart(QMap<QString,QVariant>)), dataSaver, SLOT (startRecording(QMap<QString,QVariant>)));
    QObject::connect(controlPanel, SIGNAL( recordStop()), dataSaver, SLOT (stopRecording()));
    QObject::connect((controlPanel), SIGNAL( sendNote(QString) ), dataSaver, SLOT ( takeNote(QString) ));
    QObject::connect(this, SIGNAL( closeAll()), controlPanel, SLOT (close()));



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
    // This function will test which codecs are supported on host's machine
    cv::VideoWriter testVid;
//    testVid.open("test.avi", -1,20, cv::Size(640, 480), true);
    QVector<QString> possibleCodec({"DIB ", "MJPG", "MJ2C", "XVID", "FFV1", "DX50", "FLV1", "H264", "I420","MPEG","mp4v", "0000", "LAGS", "ASV1", "GREY"});
    for (int i = 0; i < possibleCodec.length(); i++) {
        testVid.open("test.avi", cv::VideoWriter::fourcc(possibleCodec[i].toStdString()[0],possibleCodec[i].toStdString()[1],possibleCodec[i].toStdString()[2],possibleCodec[i].toStdString()[3]),
                20, cv::Size(640, 480), true);
        if (testVid.isOpened()) {
            m_availableCodec.append(possibleCodec[i]);
            qDebug() << "Codec" << possibleCodec[i] << "supported for color";
            testVid.release();
        }
        else
            unAvailableCodec.append(possibleCodec[i]);
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

    // Make trace display
    if (!ucTraceDisplay.isEmpty()) {
        if (ucTraceDisplay["enabled"].toBool(true))
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
        if (ucBehaviorTracker["enabled"].toBool(true)) {
            // Behav tracker currently is hardcoded to use first behavior camera
            QSize camRes = behavCam.first()->getResolution();

            behavTracker = new BehaviorTracker(NULL, m_userConfig, m_softwareStartTime);

            QObject::connect(behavTracker, SIGNAL(sendMessage(QString)), controlPanel, SLOT( receiveMessage(QString)));
            QObject::connect(behavTracker, &BehaviorTracker::addTraceDisplay, traceDisplay, &TraceDisplayBackend::addNewTrace);
            behavTracker->createView(camRes);
            setupBehaviorTracker();
        }
    }

    connectSnS();
}
