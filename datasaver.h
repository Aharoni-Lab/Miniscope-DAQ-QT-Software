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
    void setUserConfig(QJsonObject userConfig) { m_userConfig = userConfig; }
    void setupFilePaths();
    void setRecord(bool input) {m_recording = input;}
    void setFrameBufferParameters(QString name, cv::Mat* frameBuf, qint64 *tsBuffer, int bufSize, QSemaphore* freeFrames, QSemaphore* usedFrames);

signals:

public slots:
    void startRunning();
    void startRecording();
    void stopRecording();
    void devicePropertyChanged(QString deviceName, QString propName, double propValue);

private:
    QJsonDocument constructBaseDirectoryMetaData();
    QJsonDocument constructMiniscopeMetaData(int deviceIndex);
    void saveJson(QJsonDocument document, QString fileName);
    QJsonObject m_userConfig;
    QString baseDirectory;
    QDateTime recordStartDateTime;
    QMap<QString,QString> deviceDirectory;

    QMap<QString, QMap<QString, double>> deviceProperties;

    // Probably shoud turn all of this into a single struct
    QMap<QString, cv::Mat*> frameBuffer;
    QMap<QString, int> framesPerFile;
    QMap<QString, int> bufferSize;
    QMap<QString, quint32> savedFrameCount;
    QMap<QString, quint32> frameCount;
    QMap<QString, qint64*> timeStampBuffer;
    QMap<QString, QSemaphore*> freeCount;
    QMap<QString, QSemaphore*> usedCount;
    QMap<QString, cv::VideoWriter*> videoWriter;
    QMap<QString, QFile*> csvFile;
    QMap<QString, QTextStream*> csvStream;

    bool m_recording;
    bool m_running;
};

#endif // DATASAVER_H
