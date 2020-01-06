#ifndef CONTROLPANEL_H
#define CONTROLPANEL_H

#include "newquickview.h"

#include <QObject>
#include <QQuickItem>
#include <QJsonObject>
#include <QTimer>

class ControlPanel : public QObject
{
    Q_OBJECT
public:
    explicit ControlPanel(QObject *parent = nullptr, QJsonObject userConfig = QJsonObject());
    void createView();
    void connectSnS();
//    void setUserConfig(QJsonObject userConfig) {m_userConfig = userConfig;}

public slots:
    void receiveMessage(QString msg);
    void onRecordActivated();
    void onStopActivated();
    void recordTimerTick();
    void handleNoteSumbit(QString note);

signals:
    void recordStart();
    void recordStop();
    void sendNote(QString note);

private:
    NewQuickView *view;
    QObject *rootObject;
    QQuickItem  *messageTextArea;
    QQuickItem *recordTimeText;

    QJsonObject m_userConfig;
    double currentRecordTime;
    int m_ucRecordLengthinSeconds;

    QTimer *recordTimer;


};

#endif // CONTROLPANEL_H
