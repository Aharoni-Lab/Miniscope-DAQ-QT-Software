#include "ephysdevice.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QObject>
#include <QVariant>
#include <QFile>
#include <QCoreApplication>

#include <libusb.h>

EphysDevice::EphysDevice(QObject *parent, QJsonObject ucDevice, qint64 softwareStartTime) :
    QObject(parent),
    m_deviceProps(new ephysDeviceProps_t),
    m_errors(0)
{
    m_ucDevice = ucDevice;
    m_softStartTime = softwareStartTime;
    m_cDevice = getDeviceConfig(m_ucDevice["type"].toString());
    parseConfig();


    // Setup libusb
    if (libusb_init(&usb_ctx) < 0) {
        own_usb_ctx = false;
        usb_ctx = NULL;
        qDebug() << "Issue with libusb_init.";
    }
    else {
        own_usb_ctx = true;
        findDevice();

    }

    // Start thread
    handlerThread = new QThread;
    m_worker = new EphysDeviceWorker(nullptr, usb_ctx);
    m_worker->moveToThread(handlerThread);

    QObject::connect(handlerThread, SIGNAL (started()), m_worker, SLOT (startThread()));
    QObject::connect(handlerThread, SIGNAL (finished()), handlerThread, SLOT (deleteLater()));

    handlerThread->start();


    // test out libusb transfers:
    m_deviceProps->transfer = libusb_alloc_transfer(0);
    libusb_fill_bulk_transfer(m_deviceProps->transfer,
                              m_deviceProps->d_h,
                              m_deviceProps->epBulkOut,
                              m_deviceProps->bufOut,
                              sizeof(m_deviceProps->bufOut),
                              transferCB,
                              NULL,
                              3000);
    // submit
    libusb_submit_transfer(m_deviceProps->transfer);
    // cb gets called
    // deallocation


}

QJsonObject EphysDevice::getDeviceConfig(QString deviceType)
{
    QString jsonFile;
    QFile file;
    QJsonObject jObj;
    bool status = false;
    file.setFileName("deviceConfigs/ephysDevices.json");
    status = file.open(QIODevice::ReadOnly | QIODevice::Text);
    if (status == true) {

        jsonFile = file.readAll();
        file.close();
        QJsonDocument d = QJsonDocument::fromJson(jsonFile.toUtf8());
        jObj = d.object();
        return jObj[deviceType].toObject();
    }
    else {
        qDebug() << "Cannot open ephysDevices.json";
        m_errors |= VIDEODEVICES_JSON_LOAD_FAIL;
        return jObj; // empty json object
    }

}

void EphysDevice::parseConfig()
{
    m_deviceProps->vendorID = m_cDevice["vendorID"].toInt(-1);
    m_deviceProps->productID = m_cDevice["productID"].toInt(-1);
    m_deviceProps->epBulkIn = m_cDevice["endpointBulkIn"].toInt(-1);
    m_deviceProps->epBulkOut = m_cDevice["endpointBulkOut"].toInt(-1);
    m_deviceProps->usbConfig = m_cDevice["usbConfiguration"].toInt(-1);
    m_deviceProps->usbInterface = m_cDevice["usbInterface"].toInt(-1);
    m_deviceProps->usbAltSetting = m_cDevice["usbAltSetting"].toInt(-1);
}

void EphysDevice::findDevice()
{
    // Notes:
    // When using cypress:
    // Interface 2 with 0x04 endpoint for bulk seems to work.

    // For Atmel MCU, use Zadig to install WinUSB driver for device. Software seems to still not handle getting active config well but transfers seem to work.




    libusb_device **devs;
    int r;
    ssize_t cnt;

    struct libusb_device_descriptor desc;
//    struct libusb_config_descriptor **configDesc = NULL;
//    const struct libusb_endpoint_descriptor *epdesc;
//    const struct libusb_interface_descriptor *interdesc;

    libusb_device *dev;


    cnt = libusb_get_device_list(NULL, &devs);
    if (cnt < 0){
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
            else if (desc.idVendor == m_deviceProps->vendorID && desc.idProduct == m_deviceProps->productID){

                m_deviceProps->dev = dev;
                m_deviceProps->desc = desc;
                openDevice();
                libusb_free_device_list(devs, 1);
                break;


//                qDebug() << "VendorID: " << desc.idVendor << " | ProductID: " << desc.idProduct << " | Bus Number:" << libusb_get_bus_number(dev) << " | Device Address:" << libusb_get_device_address(dev);

//                r = libusb_get_port_numbers(dev, path, sizeof(path));
//                if (r > 0) {
//                    qDebug() <<"Paths:";
//                    for (j = 0; j < r; j++)
//                        qDebug() << "    " << j << ": " << path[j];
//                }

            }
        }
    }
    // libusb_get_active_config_descriptor causes a seg fault most of the time I think
//                    r = libusb_get_active_config_descriptor(dev,configDesc);
//                    qDebug() << "DFODNSIODSFNOIDSFNIOSDFNSDF";
//                    if (r < 0)
//                        qDebug() << "Config Desc failed:" << r;
//                    else {
//                        int numAltSettings = configDesc[0]->interface->num_altsetting;
//                        qDebug() << "Number of alt settings: " << numAltSettings;
//                        interdesc = configDesc[0]->interface->altsetting;
//                        for (int w=0; w < numAltSettings; w++) {
//                            qDebug() << "Number of endpoints: "<< interdesc[w].bNumEndpoints;
//                            for(int m=0; m < interdesc[w].bNumEndpoints; m++) {
//                                epdesc = &interdesc[w].endpoint[m];
//                                qDebug()<<"Descriptor Type: "<<(int)epdesc->bDescriptorType;
//                                qDebug()<<"Attributes Type: "<<(int)epdesc->bmAttributes;
//                                qDebug()<<"EP Address: "<<(int)epdesc->bEndpointAddress;
//                            }
//                        }
//                        qDebug() << "HERE~!!";
//                        libusb_free_config_descriptor(configDesc[0]);
//                    }



}

void EphysDevice::openDevice()
{
    int r;
    r = libusb_open(m_deviceProps->dev, &m_deviceProps->d_h);
    if (r < 0) {
        qDebug() << "libusb_open failed. Error code is: " << r;
    }
    else {
        unsigned char name[200];
        r = libusb_get_string_descriptor_ascii(m_deviceProps->d_h, m_deviceProps->desc.iProduct, name, 200);
        if ( r > 0) {
            qDebug() << "iProduct Name: " << QString::fromUtf8((char *)name, r);
        }

        // For Cypress Bulk, use interface 2 and endpoint 0x04|LIBUSB_ENDPOINT_ IN/OUT

        int conf;
        libusb_get_configuration(m_deviceProps->d_h,&conf);
        qDebug() << "Get Config: " << conf;
        r = libusb_set_configuration(m_deviceProps->d_h,m_deviceProps->usbConfig);
        if (r < 0)
            qDebug() << "set configuration issue: " << libusb_error_name(r);
         if (1){
            r = libusb_claim_interface(m_deviceProps->d_h,m_deviceProps->usbInterface);

            if (r < 0)
                qDebug() << "claim interface issue: " << r;
            else {
                r = libusb_set_interface_alt_setting(m_deviceProps->d_h, m_deviceProps->usbInterface, m_deviceProps->usbAltSetting);
                if (r < 0)
                    qDebug() << "set alt interface issue: " << r;
                else {
                    m_deviceProps->isConnected = true;
                }
            }
         }
    }
}

void LIBUSB_CALL EphysDevice::transferCB(libusb_transfer *transfer)
{
    qDebug() << "In callback function!!!!" << transfer->actual_length << transfer->status;
}

void EphysDevice::close()
{
    qDebug() << "Ephys Device close function called";
    if (m_deviceProps->d_h != NULL) {
        m_worker->event_thread_run = 0;
        libusb_release_interface(m_deviceProps->d_h, m_deviceProps->usbInterface);
        libusb_close(m_deviceProps->d_h);
    }
    libusb_exit(usb_ctx);
}

EphysDeviceWorker::EphysDeviceWorker(QObject *parent, libusb_context *ctx) :
    QObject(parent)
{
    m_ctx = ctx;
    event_thread_run = 0;
}

void EphysDeviceWorker::event_thread_func()
{
    while (event_thread_run == 1) {
        libusb_handle_events(m_ctx);

        // Get any new events
        QCoreApplication::processEvents();
    }
}

void EphysDeviceWorker::startThread()
{
    event_thread_run = 1;
    event_thread_func();
}

void EphysDeviceWorker::stopThread()
{

}
