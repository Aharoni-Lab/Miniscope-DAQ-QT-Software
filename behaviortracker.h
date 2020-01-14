#ifndef BEHAVIORTRACKER_H
#define BEHAVIORTRACKER_H

#include <opencv2/opencv.hpp>

#include <QObject>
#include <QJsonObject>
#include <QAtomicInt>
#include <QMap>
#include <QString>

class BehaviorTracker : public QObject
{
    Q_OBJECT
public:
    explicit BehaviorTracker(QObject *parent = nullptr, QJsonObject userConfig = QJsonObject());
    void parseUserConfigTracker();
    void loadCamCalibration(QString name);
    void setBehaviorCamBufferParameters(QString name, cv::Mat* frameBuf, int bufSize, QAtomicInt* acqFrameNum);
    void cameraCalibration();

signals:

public slots:

private:
    // Info from behavior cameras
    QMap<QString, cv::Mat*> frameBuffer;
    QMap<QString, QAtomicInt*> m_acqFrameNum;
    QMap<QString, int> bufferSize;

    cv::Mat currentFrame;
    QJsonObject m_userConfig;

};

#endif // BEHAVIORTRACKER_H
