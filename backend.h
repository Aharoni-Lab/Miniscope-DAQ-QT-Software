#ifndef BACKEND_H
#define BACKEND_H

#include <QObject>
#include <QJsonObject>


class backEnd : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString userConfigFileName READ userConfigFileName WRITE setUserConfigFileName NOTIFY userConfigFileNameChanged)
    Q_PROPERTY(QString userConfigDisplay READ userConfigDisplay WRITE setUserConfigDisplay NOTIFY userConfigDisplayChanged)

public:
    explicit backEnd(QObject *parent = nullptr);

    QString userConfigFileName();
    void setUserConfigFileName(const QString &input);

    QString userConfigDisplay();
    void setUserConfigDisplay(const QString &input);

    void parseUserConfigFile();

signals:
    void userConfigFileNameChanged();
    void userConfigDisplayChanged();

public slots:

private:
    QString m_userConfigFileName;
    QString m_userConfigDisplay;
    QJsonObject m_userConfig;
};

#endif // BACKEND_H
