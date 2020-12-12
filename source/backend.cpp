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

#ifdef USE_USB
 #include <libusb.h>
#endif

backEnd::backEnd(QObject *parent) :
    QObject(parent),
    m_versionNumber(""),
    m_userConfigFileName(""),
    m_userConfigOK(false),
    behavTracker(nullptr)
{
#ifdef DEBUG
//    QString homePath = QDir::homePath();
    m_userConfigFileName = "./userConfigs/UserConfigExample.json";
//    loadUserConfigFile();
    handleUserConfigFileNameChanged();

//    setUserConfigOK(true);
#endif

//    m_userConfigOK = false;

    // User Config default values
    researcherName = "";
    dataDirectory = "";
    experimentName = "";
    animalName = "";
    dataStructureOrder = {"researcherName", "experimentName", "animalName", "date"};

    ucExperiment["type"] = "None";
    ucMiniscopes = {"None"};
    ucBehaviorCams = {"None"};
    ucBehaviorTracker["type"] = "None";

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
    parseUserConfig();
    checkUserConfigForIssues();
}

void backEnd::connectSnS()
{

    // Start and stop recording signals
    QObject::connect(controlPanel, SIGNAL( recordStart()), dataSaver, SLOT (startRecording()));
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
        dataSaver->setPoseBufferParameters(behavTracker->getPoseBufferPointer(),
                                           behavTracker->getPoseFrameNumBufferPointer(),
                                           behavTracker->getPoseBufferSize(),
                                           behavTracker->getFreePosePointer(),
                                           behavTracker->getUsedPosePointer());

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

    // Main JSON header
    researcherName = m_userConfig["researcherName"].toString();
    dataDirectory= m_userConfig["dataDirectory"].toString();
    dataStructureOrder = m_userConfig["dataStructureOrder"].toArray();
    experimentName = m_userConfig["experimentName"].toString();
    animalName = m_userConfig["animalName"].toString();

    // JSON subsections
    ucExperiment = m_userConfig["experiment"].toObject();
    ucMiniscopes = devices["miniscopes"].toArray();
    ucBehaviorCams = devices["cameras"].toArray();
    ucBehaviorTracker = m_userConfig["behaviorTracker"].toObject();


}

void backEnd::setupBehaviorTracker()
{
    for (int i = 0; i < behavCam.length(); i++) {
        behavTracker->setBehaviorCamBufferParameters(behavCam[i]->getDeviceName(),
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
    for (int i = 0; i < ucMiniscopes.size(); i++) {
        tempName = ucMiniscopes[i].toObject()["deviceName"].toString();
        if (!deviceNames.contains(tempName))
            deviceNames.append(tempName);
        else {
            repeatingDeviceName = true;
            break;
        }
    }
    for (int i = 0; i < ucBehaviorCams.size(); i++) {
        tempName = ucBehaviorCams[i].toObject()["deviceName"].toString();
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
    for (int i = 0; i < ucMiniscopes.size(); i++) {
        tempName = ucMiniscopes[i].toObject()["compression"].toString("Empty");
        if (!m_availableCodec.contains(tempName) && tempName != "Empty")
            return false;
    }
    for (int i = 0; i < ucBehaviorCams.size(); i++) {
        tempName = ucBehaviorCams[i].toObject()["compression"].toString("Empty");
        if (!m_availableCodec.contains(tempName) && tempName != "Empty")
            return false;
    }
    return true;
}

void backEnd::constructUserConfigGUI()
{
    int idx;

    // Load main control GUI
    controlPanel = new ControlPanel(this, m_userConfig);
    QObject::connect(this, SIGNAL (sendMessage(QString) ), controlPanel, SLOT( receiveMessage(QString)));

    for (idx = 0; idx < ucMiniscopes.size(); idx++) {
        miniscope.append(new Miniscope(this, ucMiniscopes[idx].toObject()));
        QObject::connect(miniscope.last(),
                         SIGNAL (onPropertyChanged(QString, QString, QVariant)),
                         dataSaver,
                         SLOT (devicePropertyChanged(QString, QString, QVariant)));

        // Connect send and receive message to textbox in controlPanel
        QObject::connect(miniscope.last(), SIGNAL(sendMessage(QString)), controlPanel, SLOT( receiveMessage(QString)));
        if (miniscope.last()->getErrors() != 0) {
            // Errors have occured in creating this object
            sendMessage("ERROR: " + miniscope.last()->getDeviceName() + " has error: " + QString::number(miniscope.last()->getErrors()));
        }
        else
            miniscope.last()->createView();
    }
    for (idx = 0; idx < ucBehaviorCams.size(); idx++) {
        behavCam.append(new BehaviorCam(this, ucBehaviorCams[idx].toObject()));
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
    if (!ucExperiment.isEmpty()){
        // Construct experiment interface
    }
    if (!ucBehaviorTracker.isEmpty()) {
        behavTracker = new BehaviorTracker(NULL, m_userConfig);
        QObject::connect(behavTracker, SIGNAL(sendMessage(QString)), controlPanel, SLOT( receiveMessage(QString)));
        behavTracker->createView();
        setupBehaviorTracker();
    }


    connectSnS();
}
