#ifndef DATASAVER_H
#define DATASAVER_H

#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QDateTime>
#include <QSemaphore>
#include <QJsonDocument>
#include <QFile>
#include <QTextStream>
#include <QAtomicInt>
#include <QVariant>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

// TODO: connect to device buffers and semaphores
class DataSaver : public QObject
{
    Q_OBJECT
public:
    explicit DataSaver(QObject *parent = nullptr);
    ~DataSaver() override;
    void setUserConfig(QJsonObject userConfig) { m_userConfig = userConfig; }
    bool setupFilePaths();
    void setRecord(bool input) {m_recording = input;}
    void setFrameBufferParameters(QString name, cv::Mat* frameBuf, qint64 *tsBuffer, float *bnoBuf, qint64 *daqFrameNumBuf, int bufSize, QSemaphore* freeFrames, QSemaphore* usedFrames, QAtomicInt* acqFrame);
    void setHeadOrientationConfig(QString name, bool enable, bool filter) {headOrientationStreamState[name] = enable; headOrientationFilterState[name] = filter; }
    void setupBaseDirectory();
    void setROI(QString name, int *bbox);

    // Inspection accessors (also pin the behavior above down in unit tests)
    bool isRecording() const { return m_recording; }
    QString getBaseDirectory() const { return baseDirectory; }
    void setRecordStartDateTime(const QDateTime &dateTime) { recordStartDateTime = dateTime; }
    bool getHeadOrientationStreamState(QString name) const { return headOrientationStreamState.value(name, false); }
    bool getHeadOrientationFilterState(QString name) const { return headOrientationFilterState.value(name, false); }

    void setPoseBufferParameters(QVector<float>* poseBuf, int* poseFrameNumBuf, int poseBufSize, QSemaphore* freePos, QSemaphore* usedPos);

signals:
    void sendMessage(QString msg);
    // The folder this recording writes into, emitted once every file in it has
    // been created. baseDirectory itself belongs to the saver thread, so the
    // path travels to the GUI thread as a signal value rather than being read
    // out from under the writer - that's what lets the UI offer "open the data
    // folder" while a recording runs.
    void recordDirectoryReady(QString path);
    // Emitted when a recording could not start (a save-path file failed to
    // open) or had to stop (disk full, video file creation failed), so the UI
    // can drop its "Recording" state instead of silently saving nothing.
    void recordingFailed();

public slots:
    void startRunning();
    void stopRunning();
    void startRecording(QMap<QString,QVariant> ucInfo);
    void stopRecording();
    void devicePropertyChanged(QString deviceName, QString propName, QVariant propValue);
    void takeScreenShot(QString type);
    void takeNote(QString note);
    void setDataCompression(QString name, QString type);

private:
    bool writeBufferedFrame(const QString &name);
    void releaseRecordingFiles();
    QJsonDocument constructBaseDirectoryMetaData();
    QJsonDocument constructDeviceMetaData(QString type, QString deviceName);
    void saveJson(QJsonDocument document, QString fileName);
    QJsonObject m_userConfig;
    QString baseDirectory;
    QDateTime recordStartDateTime;
    qint64 recordStartTimeMs = 0;   // monotonic clock base for all CSV times
    QMap<QString,QString> deviceDirectory;

    QMap<QString, QMap<QString, QVariant>> deviceProperties;

    // Probably shoud turn all of this into a single struct
    QMap<QString, cv::Mat*> frameBuffer;
    QMap<QString, int> framesPerFile;
    QMap<QString, int> bufferSize;
    QMap<QString, QAtomicInt*> acqFrameNum;
    QMap<QString, quint32> savedFrameCount;
    QMap<QString, quint32> frameCount;
    QMap<QString, float*> bnoBuffer;
    QMap<QString, qint64*> daqFrameNumBuffer;
    QMap<QString, bool> headOrientationStreamState;
    QMap<QString, bool> headOrientationFilterState;
    QMap<QString, qint64*> timeStampBuffer;
    QMap<QString, QSemaphore*> freeCount;
    QMap<QString, QSemaphore*> usedCount;
    QMap<QString, cv::VideoWriter*> videoWriter;
    QMap<QString, int> dataCompressionFourCC;

    QMap<QString, QFile*> csvFile;
    QMap<QString, QTextStream*> csvStream;

    QMap<QString, QFile*> headOriFile;
    QMap<QString, QTextStream*> headOriStream;

    QFile* behavTrackerFile;
    QTextStream* behavTrackerStream;

    QMap<QString, int*> ROI;

    QFile* noteFile;
    QTextStream* noteStream;

    // Pose pointers (turn into struct!!!!!)
    QVector<float>* poseBuffer;
    int* poseFrameNumBuffer;
    int poseBufferSize;
    int btPoseCount;
    QSemaphore* freePoses;
    QSemaphore* usedPoses;
    bool behaviorTrackerEnabled;

    bool m_recording;
    bool m_running;
};

#endif // DATASAVER_H
