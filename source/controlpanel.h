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
    ~ControlPanel() override;
    void createView();
    void connectSnS();
    void fillUCEditText();
//    void setUserConfig(QJsonObject userConfig) {m_userConfig = userConfig;}

public slots:
    void receiveMessage(QString msg);
    void onRecordActivated();
    void onStopActivated();
    void onRecordingFailed();
    void recordTimerTick();
    void handleNoteSumbit(QString note);
    void extTriggerSwitchToggled2(bool checkedState);
    void extTriggerTriggered(bool state);
    void close();

signals:
    void recordStart(QMap<QString,QVariant> ucInfo);
    void recordStop();
    void sendNote(QString note);
    void setExtTriggerTrackingState(bool state);

private:
    NewQuickView *view;

public:
    // The control panel's window, for embedding as a pane in the Acquire view.
    QQuickView *panelView() const { return view; }

private:
    QObject *rootObject;
    QQuickItem  *messageTextArea;
    QQuickItem *recordTimeText;

    QJsonObject m_userConfig;
    double currentRecordTime;
    int m_ucRecordLengthinSeconds;

    QTimer *recordTimer;
    bool m_recording;


};

#endif // CONTROLPANEL_H
