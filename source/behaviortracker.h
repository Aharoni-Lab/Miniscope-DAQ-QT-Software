#ifndef BEHAVIORTRACKER_H
#define BEHAVIORTRACKER_H

#include "newquickview.h"

#include <opencv2/opencv.hpp>

#include <QObject>
#include <QJsonObject>
#include <QAtomicInt>
#include <QMap>
#include <QString>
#include <QDebug>
#include <QQuickItem>

class BehaviorTracker : public QObject
{
    Q_OBJECT
public:
    explicit BehaviorTracker(QObject *parent = nullptr, QJsonObject userConfig = QJsonObject());
    void parseUserConfigTracker();
    void loadCamCalibration(QString name);
    void setBehaviorCamBufferParameters(QString name, cv::Mat* frameBuf, int bufSize, QAtomicInt* acqFrameNum);
    void cameraCalibration();
    void createView();
    void connectSnS();
    void setUpDLCLive();
    QList<double> getDLCLivePose(cv::Mat frame);


signals:
    void sendMessage(QString msg);

public slots:
    void testSlot(QString msg) { qDebug() << msg; }
    void startRunning(); // Slot gets called when thread starts
    void close();

private:
    QString m_trackerType;
    int numberOfCameras;
    // Info from behavior cameras
    QMap<QString, cv::Mat*> frameBuffer;
    QMap<QString, QAtomicInt*> m_acqFrameNum;
    QMap<QString, int> bufferSize;

    QMap<QString, cv::Mat> currentFrame;
    QMap<QString, int> currentFrameNumberProcessed;
    QJsonObject m_userConfig;

    // For GUI
    NewQuickView *view;
    QObject *rootObject;

    bool m_trackingRunning;

};

#endif // BEHAVIORTRACKER_H
