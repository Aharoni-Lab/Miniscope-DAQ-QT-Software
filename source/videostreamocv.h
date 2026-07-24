#ifndef VIDEOSTREAMOCV_H
#define VIDEOSTREAMOCV_H

#include <QObject>
#include <QString>
#include <opencv2/core/core.hpp>
#include <opencv2/videoio.hpp>
#include <QVector>

#include "miniscopeprotocol.h"
#include "videostreambase.h"

#ifdef Q_OS_MACOS
#include "avfframegrabbermac.h"
#endif


class VideoStreamOCV : public VideoStreamBase
{
    Q_OBJECT
public:
    explicit VideoStreamOCV(QObject *parent = nullptr, int width = 0, int height = 0, double pixelClock = 0);
    ~VideoStreamOCV() override;
    int connect2Camera(int cameraID) override;
    int connect2Video(QString folderPath, QString filePrefix, float playbackFPS) override;

public slots:
    void startStream() override;
    void startRecording() override;
    void stopRecording() override;
    void openCamPropsDialog() override;

protected:
    void sendCommands() override;   // drops the queue when there is no control channel
    bool writeControlWord(quint8 selector, quint16 word) override;   // CAP_PROP passthrough
    bool readControl(quint8 selector, quint16 *value) override;      // CAP_PROP passthrough
    bool attemptReconnect() override;

private:
    cv::VideoCapture *cam;

    QString m_connectionType;

#ifdef Q_OS_MACOS
    // Live cameras on macOS stream through AVFoundation pinned to the
    // device's uniqueID (m_connectionType == "AVF"); cv::VideoCapture is only
    // used for video-file playback there. See connect2Camera.
    AvfFrameGrabber m_grabber;
    QString m_avfUniqueID;
    QString m_avfName;
#endif

    double m_playbackFPS;
    QString m_playbackFolderPath;
    QString m_playbackFilePrefix;
    int m_playbackFileIndex;

};

#endif // VIDEOSTREAMOCV_H
