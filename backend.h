#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>

#include <miniscope.h>


class backEnd : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString userConfigFileName READ userConfigFileName WRITE setUserConfigFileName NOTIFY userConfigFileNameChanged)
    Q_PROPERTY(QString userConfigDisplay READ userConfigDisplay WRITE setUserConfigDisplay NOTIFY userConfigDisplayChanged)
    Q_PROPERTY(bool userConfigOK READ userConfigOK WRITE setUserConfigOK NOTIFY userConfigOKChanged)
public:
    explicit backEnd(QObject *parent = nullptr);

    QString userConfigFileName();
    void setUserConfigFileName(const QString &input);
    void setUserConfigOK(bool userConfigOK) {m_userConfigOK = userConfigOK;}
    bool userConfigOK() {return m_userConfigOK;}

    QString userConfigDisplay();
    void setUserConfigDisplay(const QString &input);

    void loadUserConfigFile();
    bool checkUserConfigForIssues();
    void constructUserConfigGUI();
    void parseUserConfig();

signals:
    void userConfigFileNameChanged();
    void userConfigDisplayChanged();
    void userConfigOKChanged();

public slots:
    void onRunClicked();

private:
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

    QVector<Miniscope*> miniscope;

};

#endif // BACKEND_H
