#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QString>

#include "miniscope.h"
#include "behaviorcam.h"
#include "controlpanel.h"
#include "datasaver.h"
#include "behaviortracker.h"
#include "tracedisplay.h"


class backEnd : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString userConfigFileName READ userConfigFileName WRITE setUserConfigFileName NOTIFY userConfigFileNameChanged)
    Q_PROPERTY(QString userConfigDisplay READ userConfigDisplay WRITE setUserConfigDisplay NOTIFY userConfigDisplayChanged)
    Q_PROPERTY(bool userConfigOK READ userConfigOK WRITE setUserConfigOK NOTIFY userConfigOKChanged)
    Q_PROPERTY(QString availableCodecList READ availableCodecList WRITE setAvailableCodecList NOTIFY availableCodecListChanged)
    Q_PROPERTY(QString versionNumber READ versionNumber WRITE setVersionNumber NOTIFY versionNumberChanged)
public:
    explicit backEnd(QObject *parent = nullptr);

    QString userConfigFileName() {return m_userConfigFileName;}
    void setUserConfigFileName(const QString &input);

    bool userConfigOK() {return m_userConfigOK;}
    void setUserConfigOK(bool userConfigOK) {m_userConfigOK = userConfigOK;}

    QString userConfigDisplay(){ return m_userConfigDisplay; }
    void setUserConfigDisplay(const QString &input);

    QString availableCodecList(){ return m_availableCodecList; }
    void setAvailableCodecList(const QString &input);

    QString versionNumber() { return m_versionNumber; }
    void setVersionNumber(const QString &input) { m_versionNumber = input; }

    void loadUserConfigFile();
    bool checkUserConfigForIssues();
    void constructUserConfigGUI();
    void parseUserConfig();

    void setupBehaviorTracker();

    bool checkForUniqueDeviceNames();
    bool checkForCompression();

signals:
    void userConfigFileNameChanged();
    void userConfigDisplayChanged();
    void userConfigOKChanged();
    void availableCodecListChanged();
    void versionNumberChanged();

    void closeAll();
    void showErrorMessage();
    void showErrorMessageCompression();
    void sendMessage(QString);

public slots:
    void onRunClicked();
    void onRecordClicked();
    void exitClicked();
    void handleUserConfigFileNameChanged();
//    void onStopClicked();

private:
    void connectSnS();
    void setupDataSaver();

    void testCodecSupport();

    QString m_versionNumber;
    QString m_userConfigFileName;
    QString m_userConfigDisplay;
    bool m_userConfigOK;
    QJsonObject m_userConfig;

    // Break down of different types in user config file
    // 'uc' stands for userConfig
    QString researcherName;
    QString dataDirectory;
    QJsonArray dataStructureOrder;
    QString experimentName;
    QString animalName;

    QJsonObject ucExperiment;
    QJsonArray ucMiniscopes;
    QJsonArray ucBehaviorCams;
    QJsonObject ucBehaviorTracker;
    QJsonObject ucTraceDisplay;

    QVector<Miniscope*> miniscope;
    QVector<BehaviorCam*> behavCam;
    ControlPanel *controlPanel;
    TraceDisplayBackend *traceDisplay;

    DataSaver *dataSaver;
    QThread *dataSaverThread;

    BehaviorTracker *behavTracker;

    QVector<QString> m_availableCodec;
    QString m_availableCodecList;
    QVector<QString> unAvailableCodec;

};

#endif // BACKEND_H
