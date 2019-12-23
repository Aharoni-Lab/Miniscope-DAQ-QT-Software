#ifndef DATASAVER_H
#define DATASAVER_H

#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QDateTime>

class DataSaver : public QObject
{
    Q_OBJECT
public:
    explicit DataSaver(QObject *parent = nullptr);
    void setUserConfig(QJsonObject userConfig) { m_userConfig = userConfig; }
    void setupFilePaths();

signals:

public slots:
    void startRecording();

private:
    QJsonObject m_userConfig;
    QString baseDirectory;
    QDateTime recordStartDateTime;
    QMap<QString,QString> deviceDirectory;
};

#endif // DATASAVER_H
