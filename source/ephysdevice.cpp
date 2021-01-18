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
#include <QSemaphore>
#include <QDateTime>

#include <libusb.h>

EphysDevice::EphysDevice(QObject *parent, QJsonObject ucDevice, qint64 softwareStartTime) :
    QObject(parent),
    m_deviceProps(new ephysDeviceProps_t),
    m_errors(0)
{
    // Set up thread safe stuff
    freeBufs = new QSemaphore;
    usedBufs  = new QSemaphore;
    acqNum = new QAtomicInt(0);
    idxInteral = 0;
    freeBufs->release(BUFFER_NUM_SAMPLES);

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

    QObject::connect(this, SIGNAL (newDataAvailable()), this, SLOT (handleNewData()));

    // Start thread
    handlerThread = new QThread;
    m_worker = new EphysDeviceWorker(nullptr, usb_ctx);
    m_worker->moveToThread(handlerThread);

    QObject::connect(handlerThread, SIGNAL (started()), m_worker, SLOT (startThread()));
    QObject::connect(handlerThread, SIGNAL (finished()), handlerThread, SLOT (deleteLater()));

    handlerThread->start();


    // test out libusb transfers:
    for (int i=0; i < NUM_TRANSFERS; i++) {
        m_deviceProps->userData[i] = new userData_t;
        m_deviceProps->userData[i]->transferNum = i;
        m_deviceProps->userData[i]->active = true;
        m_deviceProps->userData[i]->bufIn = m_deviceProps->bufIn[0];
//        m_deviceProps->userData[i]->ephysBuffer = ephysBuffer[0];
        m_deviceProps->userData[i]->timeStampBufferSoft = timeStampBufferSoft;
        m_deviceProps->userData[i]->timeStampBufferMCU = timeStampBufferMCU;
        m_deviceProps->userData[i]->freeBufs = freeBufs;
        m_deviceProps->userData[i]->usedBufs = usedBufs;
        m_deviceProps->userData[i]->acqNum = acqNum;
        m_deviceProps->userData[i]->idxInteral = &idxInteral;
        m_deviceProps->userData[i]->eDev = this;
        m_deviceProps->transfer[i] = libusb_alloc_transfer(0);
        m_deviceProps->userData[i]->transfer = m_deviceProps->transfer[i];
        libusb_fill_bulk_transfer(m_deviceProps->transfer[i],
                                  m_deviceProps->d_h,
                                  m_deviceProps->epBulkIn|LIBUSB_ENDPOINT_IN,
                                  m_deviceProps->bufIn[i],
                                  sizeof(m_deviceProps->bufIn[i]),
                                  transferCB,
                                  (void *) m_deviceProps->userData[i],
                                  3000);
        // submit
        libusb_submit_transfer(m_deviceProps->transfer[i]);
    }


    m_deviceProps->userDataOut = new userData_t;
    m_deviceProps->userDataOut->transferNum = -1;
    m_deviceProps->userDataOut->active = true;
    m_deviceProps->outTransfer = libusb_alloc_transfer(0);
    m_deviceProps->userDataOut->transfer = m_deviceProps->outTransfer;
    m_deviceProps->bufOut[0] = 1; // starts ephys streaming
    libusb_fill_bulk_transfer(m_deviceProps->outTransfer,
                              m_deviceProps->d_h,
                              m_deviceProps->epBulkOut|LIBUSB_ENDPOINT_OUT,
                              m_deviceProps->bufOut,
                              sizeof(m_deviceProps->bufOut),
                              transferCB,
                              (void *) m_deviceProps->userDataOut,
                              3000);
    // submit
    libusb_submit_transfer(m_deviceProps->outTransfer);


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
    userData_t *userData = (userData_t *) transfer->user_data;
//    uint32_t *buf = (uint32_t *) transfer->buffer;
    uint32_t header0 = transfer->buffer[0] |
            transfer->buffer[1]<<8 |
            transfer->buffer[2] << 16 |
            transfer->buffer[3] << 24;
    uint32_t header1 = transfer->buffer[4] |
            transfer->buffer[5]<<8 |
            transfer->buffer[6] << 16 |
            transfer->buffer[7] << 24;
    qDebug() << "In callback function!!!!" << userData->transferNum << transfer->actual_length << transfer->status <<
                header0 << header1;

    if (userData->transferNum >= 0) {
        int resubmit = 1;
        switch (transfer->status) {
        case LIBUSB_TRANSFER_COMPLETED:
            if (transfer->num_iso_packets == 0) {
                // This is bulk transfer
                processPayload(userData, transfer->buffer, transfer->actual_length);
            }
            break;
        case LIBUSB_TRANSFER_CANCELLED:
        case LIBUSB_TRANSFER_ERROR:
        case LIBUSB_TRANSFER_NO_DEVICE: {
            qDebug() << "USB Transfer issue!";
            libusb_free_transfer(transfer);
            userData->transfer = NULL;
            userData->active = false;
            resubmit = 0;
            break;
        }
        case LIBUSB_TRANSFER_TIMED_OUT:
        case LIBUSB_TRANSFER_STALL:
        case LIBUSB_TRANSFER_OVERFLOW:
            qDebug() << "Retrying transfer!";
            break;
        }

        if (resubmit) {
            int r = libusb_submit_transfer(transfer);
            if (r < 0) {
                // resubmit failed
                libusb_free_transfer(transfer);
                userData->transfer = NULL;
                userData->active = false;
            }
        }
    }
    else {
        qDebug() << "Bulk out finished!";
        libusb_free_transfer(transfer);
    }
}

void EphysDevice::processPayload(userData_t *userData, uint8_t *payload, int payloadLength)
{

    int headerLength = 128;

    if (payloadLength == 0) {
        return; // Empty payload
    }
    if (payloadLength == 2048) {
        // This is a normal transfer
        int idx = *userData->eDev->acqNum % BUFFER_NUM_SAMPLES;
        // Fill buffer
        if ((idx + 30) < BUFFER_NUM_SAMPLES) {
            memcpy(&userData->eDev->ephysBuffer[idx * 32] , payload + headerLength, 2048 - headerLength);
//            qDebug() << "PAYLOAD" << payload[128] << payload[129] << payload[128 + 64] << payload[129 + 64];
//            qDebug() << "EPHYS" << userData->eDev->ephysBuffer[idx][0] << userData->eDev->ephysBuffer[idx+1][0];
        }
        else {
            int topCount = BUFFER_NUM_SAMPLES - idx;
            memcpy((uint8_t *)&userData->eDev->ephysBuffer[idx * 32] , payload + headerLength, (topCount * 32 * 2));
            memcpy((uint8_t *)&userData->eDev->ephysBuffer[0] , payload + headerLength + (topCount * 32 * 2), 2048 - headerLength - (topCount * 32 * 2));
        }
        userData->eDev->timeStampBufferSoft[idx] = QDateTime().currentMSecsSinceEpoch();
        userData->eDev->timeStampBufferMCU[idx] = payload[4] |
                payload[5] << 8 |
                payload[6] << 16 |
                payload[7] << 24;

//        if (userData->eDev->freeBufs->tryAcquire()) {

            userData->eDev->acqNum->operator+=(30);
            *userData->idxInteral+= 30;
            qDebug() << "EPHYS" << &userData->eDev->ephysBuffer[idx * 32] << &userData->eDev->ephysBuffer[(idx+1)*32];
            userData->eDev->handleNewData();
//            userData->eDev->usedBufs->release();

            // Remove need for datasaver right now
//            userData->usedBufs->tryAcquire();
//            userData->freeBufs->release();
//        }
//        else {
//            if (userData->eDev->freeBufs->available() == 0) {
//                QThread::msleep(100);
//            }
//        }
    }
    else {
        qDebug() << "Uh oh, length of USB transfer is not 2048. It is" << payloadLength;
    }
}

void EphysDevice::setupTraceDisplay()
{
    qDebug() << "SETUP EPHYS DISPLAY!";
    for (int i=0; i < NUM_MAX_TRACES; i++) {
        m_traceDisplayBufNum[i] = 1;
        m_traceNumDataInBuf[i][0] = 0;
        m_traceNumDataInBuf[i][1] = 0;
        m_traceColors[i][0] = (float) ((int)(i + 0.3) % 8) / 7.0f;
        m_traceColors[i][1] = -2.0f;
        m_traceColors[i][2] = -2.0f;

        emit addTraceDisplay("Ephys" + QString::number(i),
                             m_traceColors[i],
                             2.0f / (float)(1<<16),
                             "val",
                             false,
                             &m_traceDisplayBufNum[i],
                             m_traceNumDataInBuf[i],
                             TRACE_DISPLAY_BUFFER_LENGTH,
                             m_traceDisplayT[i][0],
                             m_traceDisplayY[i][0]);
    }
    qDebug() << "SETUP EPHYS DISPLAY!2";

}

void EphysDevice::close()
{
    qDebug() << "Ephys Device close function called";
    if (m_deviceProps->d_h != NULL) {
        m_worker->event_thread_run = 0;
        for (int i=0; i < NUM_TRANSFERS; i++) {
            if (m_deviceProps->userData[i]->active) {
                libusb_cancel_transfer(m_deviceProps->transfer[i]);
//                libusb_free_transfer(m_deviceProps->transfer[i]);
            }
        }
        libusb_release_interface(m_deviceProps->d_h, m_deviceProps->usbInterface);
        libusb_close(m_deviceProps->d_h);
    }
    libusb_exit(usb_ctx);
}

void EphysDevice::handleNewData()
{
    int idxNum = *acqNum - 30;
//    qDebug() << "TRACE" << (idxNum) % BUFFER_NUM_SAMPLES << (idxNum + 1) % BUFFER_NUM_SAMPLES << &ephysBuffer[((idxNum) % BUFFER_NUM_SAMPLES) * 32] << &ephysBuffer[((idxNum + 1) % BUFFER_NUM_SAMPLES) * 32];
    // Lets fill some trace buffers....
    int dataCount;
    int bufNum;

    for(int i=0; i < NUM_MAX_TRACES; i++) {
        if (m_traceDisplayBufNum[i] == 0)
            bufNum = 1;
        else
            bufNum = 0;
        dataCount = m_traceNumDataInBuf[i][bufNum];
        if ((dataCount + 30) < TRACE_DISPLAY_BUFFER_LENGTH) {
            // There is more space to put data
            for (int j=0; j < 30; j++) {

                m_traceDisplayY[i][bufNum][dataCount + j] = ephysBuffer[((idxNum + j) % BUFFER_NUM_SAMPLES) * 32 + i] - (1<<15);
                m_traceDisplayT[i][bufNum][dataCount + j] = (timeStampBufferSoft[(idxNum) % BUFFER_NUM_SAMPLES] - m_softStartTime)/1000.0f;
                m_traceNumDataInBuf[i][bufNum]++;
            }
        }
        else {
            // Handle case when display buffer is almost full.
            // This probably almost never happens as the tracedisplay would need to be lagging a ton.
        }
    }

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
