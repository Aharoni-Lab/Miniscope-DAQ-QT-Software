#ifndef EPHYSDEVICE_H
#define EPHYSDEVICE_H

#include <QObject>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QVariant>
#include <QSemaphore>
#include <QDateTime>
#include <QAtomicInt>

#include <libusb.h>

#define SAMPLE_BUFFER_SIZE 1

// ------------- ERRORS ----------------
#define VIDEODEVICES_JSON_LOAD_FAIL     1
// -------------------------------------
struct usb_device;
struct usb_context;

typedef struct EphysDeviceProps{
    int vendorID;
    int productID;
    int epBulkIn;
    int epBulkOut;
    int usbConfig;
    int usbInterface;
    int usbAltSetting;

    libusb_device *dev;
    libusb_device_handle *d_h = NULL;
    struct libusb_device_descriptor desc;

    libusb_transfer *transfer;
    uint8_t bufOut[1024];
    uint8_t bufIn[1024];

    bool isConnected = false;


} ephysDeviceProps_t;

//typedef struct usb_device {
//    struct usb_context *ctx;
//    int ref;
//    libusb_device *dev;
//    libusb_device_handle *devh;
//    struct libusb_config_descriptor *config;

//    // TODO: Add control interface
//    struct libusb_transfer *status_xfer;
//    uint8_t status_buf[32];
//    void *status_user_ptr;

//    // Steam info

//    struct usb_device *prev, *next;

//} usb_device_handle_t;


class EphysDeviceWorker : public QObject
{
    Q_OBJECT
public:
    EphysDeviceWorker(QObject *parent, struct libusb_context *ctx);
    void event_thread_func();
    QAtomicInt event_thread_run;
    libusb_context *m_ctx;
public slots:
    void startThread();
    void stopThread();
};

class EphysDevice : public QObject
{
    Q_OBJECT
public:
    EphysDevice(QObject *parent, QJsonObject ucDevice, qint64 softwareStartTime);
    QJsonObject getDeviceConfig(QString deviceType);
    void parseConfig();
    void findDevice();
    void openDevice();
    static void LIBUSB_CALL transferCB(struct libusb_transfer *transfer);


signals:
    void sendMessage(QString msg);

public slots:
    void close();


private:
    QThread* m_workerThread;
    QJsonObject m_ucDevice;
    QJsonObject m_cDevice;
    qint64 m_softStartTime;
    ephysDeviceProps_t* m_deviceProps;
    int m_errors;

    // ------ libusb ------------------
    struct libusb_context *usb_ctx;
    bool own_usb_ctx;

    QThread *handlerThread;
    EphysDeviceWorker *m_worker;
    // --------------------------------



    // ----- For thread safe buffer ---
    qint64 timeStampBuffer[SAMPLE_BUFFER_SIZE];
    QSemaphore *freeBufs;
    QSemaphore *usedBufs;
    QAtomicInt *acqNum;
    // --------------------------------
};


#endif // EPHYSDEVICE_H
