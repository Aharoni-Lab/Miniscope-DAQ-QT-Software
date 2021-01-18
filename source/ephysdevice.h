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

#define BUFFER_NUM_SAMPLES          (30000 * 30) // 30KSps * 30second = buffer length
#define NUM_CHANNELS                32

#define NUM_MAX_TRACES       32
#define TRACE_DISPLAY_BUFFER_LENGTH   20000
// ------------- ERRORS ----------------
#define VIDEODEVICES_JSON_LOAD_FAIL     1

// -------------------------------------

#define NUM_TRANSFERS                   10
#define USB_BUFFER_SIZE                 2048

struct usb_device;
struct usb_context;
class EphysDevice;

typedef struct transferUserData {
    int transferNum;
    libusb_transfer *transfer;
    bool active;
    uint8_t *bufIn;
    EphysDevice *eDev;
//    uint16_t *ephysBuffer;
    qint64 *timeStampBufferSoft;
    qint64 *timeStampBufferMCU;
    QSemaphore *freeBufs;
    QSemaphore *usedBufs;
    QAtomicInt *acqNum;
    int *idxInteral;
} userData_t;

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

    libusb_transfer *transfer[NUM_TRANSFERS];
    userData_t *userData[NUM_TRANSFERS];
    uint8_t bufIn[NUM_TRANSFERS][USB_BUFFER_SIZE];

    libusb_transfer *outTransfer;
    userData_t *userDataOut;
    uint8_t bufOut[512];




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
    static void processPayload(userData_t *userData, uint8_t *payload, int payloadLength);
    void setupTraceDisplay();


signals:
    void sendMessage(QString msg);
    void newDataAvailable();
    void addTraceDisplay(QString, float c[3], float, QString, bool sameOffset, QAtomicInt*, QAtomicInt*, int , float*, float*);


public slots:
    void close();
    void handleNewData();


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
    uint16_t ephysBuffer[BUFFER_NUM_SAMPLES*NUM_CHANNELS];
    qint64 timeStampBufferSoft[BUFFER_NUM_SAMPLES];
    qint64 timeStampBufferMCU[BUFFER_NUM_SAMPLES];
    QSemaphore *freeBufs;
    QSemaphore *usedBufs;
    QAtomicInt *acqNum;
    int idxInteral;
    // ---------------------------------

    // --------- For trace display -----
    float m_traceColors[NUM_MAX_TRACES][3];
    QAtomicInt m_traceDisplayBufNum[NUM_MAX_TRACES];
    QAtomicInt m_traceNumDataInBuf[NUM_MAX_TRACES][2];
    float m_traceDisplayY[NUM_MAX_TRACES][2][TRACE_DISPLAY_BUFFER_LENGTH];
    float m_traceDisplayT[NUM_MAX_TRACES][2][TRACE_DISPLAY_BUFFER_LENGTH];
    // --------------------------------
};


#endif // EPHYSDEVICE_H
