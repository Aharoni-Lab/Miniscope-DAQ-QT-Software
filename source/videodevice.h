#ifndef VIDEODEVICE_H
#define VIDEODEVICE_H

// This is the base class for all video type devices that stream data into this software

#include <QObject>
#include <QThread>
#include <QSemaphore>
#include <QTimer>
#include <QAtomicInt>
#include <QJsonObject>
#include <QQuickView>
#include <QMap>
#include <QVector>
#include <QQuickItem>
#include <QVariant>
#include <QString>
#include <QStringList>

#include "videostreambase.h"
#include "videostreamocv.h"
#include "videostreamlibuvc.h"
#include "videodisplay.h"
#include "newquickview.h"
#include <opencv2/opencv.hpp>

// ------- Defines for UVC to I2C communication ----------
// Will be replaced once Miniscope communication is moved to vendor class USB
#define PROTOCOL_I2C            -2
#define PROTOCOL_SPI            -3
#define SEND_COMMAND_VALUE_H    -5
#define SEND_COMMAND_VALUE_L    -6
#define SEND_COMMAND_VALUE      -6
#define SEND_COMMAND_VALUE_H16  -7
#define SEND_COMMAND_VALUE_H24  -8
#define SEND_COMMAND_VALUE2_H   -9
#define SEND_COMMAND_VALUE2_L   -10
#define SEND_COMMAND_ERROR      -20
// ---------------------------------------------------------

// ------------- ERRORS ----------------
#define VIDEODEVICES_JSON_LOAD_FAIL     1
// -------------------------------------

// ----- Size of frame buffer in RAM for each device -------
#define FRAME_BUFFER_SIZE   128
#define BASELINE_FRAME_BUFFER_SIZE  128
// ---------------------------------------------------------

class VideoDevice : public QObject
{
    Q_OBJECT
public:
    explicit VideoDevice(QObject *parent = nullptr, QJsonObject ucDevice = QJsonObject(), qint64 softwareStartTime = 0, bool preferDirectControl = false);
    // Session teardown: joins the (already-stopped) stream thread, frees the
    // stream object, and closes/frees this device's window.
    ~VideoDevice() override;
    QJsonObject getDeviceConfig(QString deviceType);
    QObject* getRootDisplayObject() { return rootObject; }
    QQuickItem* getRootDisplayChild(QString childName) { return rootObject->findChild<QQuickItem*>(childName); }
    VideoDisplay* getVideoDisplay() { return vidDisplay; }
    VideoStreamBase* getDeviceStream() { return deviceStream; }
    virtual void setupDisplayObjectPointers() { }; // Child class should override this!
    bool getHeadOrienataionStreamState() { return m_headOrientationStreamState;}
    bool getHeadOrienataionFilterState() { return m_headOrientationFilterState;}
    void createView();
    void connectSnS();
    // Orderly shutdown: stop the capture loop and join its thread. Safe to
    // call from the GUI thread; no-op if the device never connected.
    void stopAndJoinStream();
    void defineDeviceAddrs();
    void parseUserConfigDevice();
    void sendInitCommands();
    QString getCompressionType();
    cv::Mat* getFrameBufferPointer(){return frameBuffer;}
    qint64* getTimeStampBufferPointer(){return timeStampBuffer;}
    qint64* getDaqFrameNumBufferPointer(){return daqFrameNumBuffer;}
    int getBufferSize() {return FRAME_BUFFER_SIZE;}
    QSemaphore* getFreeFramesPointer(){return freeFrames;}
    QSemaphore* getUsedFramesPointer(){return usedFrames;}
    QAtomicInt* getAcqFrameNumPointer(){return m_acqFrameNum;}
    QAtomicInt* getDAQFrameNumPointer() { return m_daqFrameNum; }
    QString getDeviceName(){return m_deviceName;}
    // The device's stream window, for embedding as a pane in the Acquire view.
    QQuickView *deviceView() const { return view; }
    // Native video aspect (catalog width/height); the pane letterboxes to this.
    double displayAspectRatio() const {
        const double w = m_cDevice.value("width").toDouble();
        const double h = m_cDevice.value("height").toDouble();
        return (w > 0 && h > 0) ? w / h : 0.0;
    }
    int getErrors() { return m_errors; }
    QSize getResolution() {return m_resolution;}

    // Messages emitted during construction (connect2Camera failures etc.)
    // fire before the backend has wired sendMessage to the control panel, so
    // the constructor holds them here. The backend takes them right after
    // wiring and relays them; from then on messages flow directly.
    QStringList takeEarlyMessages();

    virtual void handleNewDisplayFrame(qint64 timeStamp, cv::Mat frame, int f, VideoDisplay* vidDisp);

    // Adding ROI control for behav and miniscopes
    int* getROI() { return m_roiBoundingBox; }

    virtual void setupBNOTraceDisplay() {} // Override

    float bnoBuffer[FRAME_BUFFER_SIZE*5]; //w,x,y,z,norm
    void setTraceDisplayStatus(bool status) {m_traceDisplayStatus = status; }
    bool getTraceDisplayStatus() { return m_traceDisplayStatus; }

signals:
    // TODO: setup signals to configure camera in thread
    void displayCreated();
    void setPropertyI2C(long preambleKey, QVector<quint8> packet);
    void onPropertyChanged(QString devieName, QString propName, QVariant propValue);
    void sendMessage(QString msg);
    void takeScreenShot(QString type);
    void setExtTriggerTrackingState(bool state);
    void extTriggered(bool state);
    void startRecording();
    void stopRecording();
    void addTraceDisplay(QString, float c[3], float, QString, bool, QAtomicInt*, QAtomicInt*, int , float*, float*);

public slots:
    void sendNewFrame();
    void testSlot(QString, double);
    void handlePropChangedSignal(QString type, double displayValue, double i2cValue, double i2cValue2);
    void handleTakeScreenShotSignal();

    void handleSaturationSwitchChanged(bool checked);
    void handleLutSwitchChanged(bool checked);
    void handleSetExtTriggerTrackingState(bool state);
    void handleRecordStart(); // Currently used to toggle LED on and off
    void handleRecordStop(); // Currently used to toggle LED on and off
    void handleInitCommandsRequest();
    void close();

    // Adding ROI control for behav and miniscopes
    void handleSetRoiClicked();
    void handleAddTraceRoiClicked();
    void handleNewROI(int leftEdge, int topEdge, int width, int height);
    virtual void handleAddNewTraceROI(int leftEdge, int topEdge, int width, int height);

    // Re-applies the stored ROI overlay at the current display size so it tracks
    // window resizes.
    void handleDisplayResized();

private:

    QSize m_resolution;
    void configureDeviceControls();
    // Live display-pixels-per-camera-pixel (x, y). Used to map ROI selections to
    // camera coordinates and back, correctly at any (resized) window size.
    QSizeF displayPerCameraScale();
    QVector<QMap<QString, int>> parseSendCommand(QJsonArray sendCommand);
    int processString2Int(QString s);
    QMap<QString,quint16> deviceAddr; //only used with Miniscopes???

    int m_camConnected;
    NewQuickView *view;
    VideoStreamBase *deviceStream;
    bool m_preferDirectControlBackend;
    QThread *videoStreamThread;
    cv::Mat frameBuffer[FRAME_BUFFER_SIZE];
    qint64 timeStampBuffer[FRAME_BUFFER_SIZE];
    qint64 daqFrameNumBuffer[FRAME_BUFFER_SIZE];
    // float bnoBuffer[FRAME_BUFFER_SIZE*5]; //w,x,y,z,norm
    QSemaphore *freeFrames;
    QSemaphore *usedFrames;
    QObject *rootObject;
    VideoDisplay *vidDisplay;
    QTimer *timer;
    int m_previousDisplayFrameNum;
    QAtomicInt *m_acqFrameNum;
    QAtomicInt *m_daqFrameNum;

    // User Config parameters
    QJsonObject m_ucDevice;
    int m_deviceID;
    QString m_deviceName;
    QString m_deviceType;
//    QMap<QString,double> m_ucParameters; // holds all number parameters


    QJsonObject m_cDevice; // Consider renaming to not confuse with ucMiniscopes
    QMap<QString,QVector<QMap<QString, int>>> m_controlSendCommand;

    QString m_compressionType;

    // Don't like this being in videodevice class but not sure what else to do
    bool m_headOrientationStreamState;
    bool m_headOrientationFilterState;
    // -----------------

    // ROI
    bool m_roiIsDefined;
    int m_roiBoundingBox[4]; // left, top, width, height


    // USED WITH MINISCOPE NOT WITH WEBCAM. MAYBE MOVE!
    double m_lastLED0Value;
    bool m_extTriggerTrackingState;

    // holds possible error states
    int m_errors;

    qint64 m_softwareStartTime;
    bool m_traceDisplayStatus;

    // See takeEarlyMessages().
    QStringList m_earlyMessages;
    bool m_holdEarlyMessages = true;

    // Display LUT (colormap) selected in the user config: 1=green, 2=red,
    // 3=inferno; the on-window switch toggles between this and 0 (grayscale).
    int m_lutColormap;
};

#endif // VIDEODEVICE_H
