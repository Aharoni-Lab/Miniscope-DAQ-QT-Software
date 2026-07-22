#ifndef VIDEOSTREAMBASE_H
#define VIDEOSTREAMBASE_H

#include <QObject>
#include <QString>
#include <QVector>
#include <opencv2/core/core.hpp>

class QSemaphore;
class QAtomicInt;

// Abstract capture-backend interface for a single video device.
//
// Two concrete backends implement this:
//   * VideoStreamOCV     - OpenCV VideoCapture (V4L2 on Linux, DirectShow on
//                          Windows). Used for all devices on Windows and for
//                          webcam / behaviour cameras everywhere.
//   * VideoStreamLibUVC  - libuvc/libusb direct UVC access (Linux only). Used
//                          for Miniscopes on Linux, where the kernel uvcvideo
//                          driver caches UVC control reads and so cannot return
//                          the live frame counter / BNO head-orientation
//                          registers that the Miniscope streams back through
//                          GET_CUR. A fresh libuvc GET_CUR bypasses that cache.
//
// VideoDevice owns one of these, moves it to its own QThread, and talks to it
// purely through this interface (direct calls + queued signals/slots), so the
// backend is interchangeable.
class VideoStreamBase : public QObject
{
    Q_OBJECT
public:
    explicit VideoStreamBase(QObject *parent = nullptr) : QObject(parent) {}
    ~VideoStreamBase() override = default;

    virtual void setBufferParameters(cv::Mat *frameBuf, qint64 *tsBuf, float *bnoBuf,
                                     int bufferSize, QSemaphore *freeFramesS, QSemaphore *usedFramesS,
                                     QAtomicInt *acqFrameNum, QAtomicInt *daqFrameNumber) = 0;
    virtual int connect2Camera(int cameraID) = 0;
    virtual int connect2Video(QString folderPath, QString filePrefix, float playbackFPS) = 0;
    virtual void setHeadOrientationConfig(bool enableState, bool filterState) = 0;
    virtual void setIsColor(bool isColor) = 0;
    virtual void setDeviceName(QString name) = 0;

signals:
    void sendMessage(QString msg);
    void newFrameAvailable(QString name, int frameNum);
    void extTriggered(bool triggerState);
    void requestInitCommands();

public slots:
    virtual void startStream() = 0;
    virtual void stopSteam() = 0;
    virtual void setPropertyI2C(long preambleKey, QVector<quint8> packet) = 0;
    virtual void setExtTriggerTrackingState(bool state) = 0;
    virtual void startRecording() = 0;
    virtual void stopRecording() = 0;
    virtual void openCamPropsDialog() = 0;
};

#endif // VIDEOSTREAMBASE_H
