#include "backend.h"
#include <QDebug>
#include <QFileDialog>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>


backEnd::backEnd(QObject *parent) : QObject(parent)
{
    m_userConfigFileName = "wsw";
}

QString backEnd::userConfigFileName()
{
    return m_userConfigFileName;
}

void backEnd::setUserConfigFileName(const QString &input)
{
    if (input != m_userConfigFileName) {
        m_userConfigFileName = input;
        //emit userConfigFileNameChanged();
    }

    parseUserConfigFile();
}

QString backEnd::userConfigDisplay()
{
    return m_userConfigDisplay;
}

void backEnd::setUserConfigDisplay(const QString &input)
{
    if (input != m_userConfigDisplay) {
        m_userConfigDisplay = input;
        emit userConfigDisplayChanged();
    }
}

void backEnd::parseUserConfigFile()
{
    QFile file;
    file.setFileName(m_userConfigFileName);
    file.open(QIODevice::ReadOnly | QIODevice::Text);
    setUserConfigDisplay(file.readAll());
    file.close();
    //qWarning() << val;
    QJsonDocument d = QJsonDocument::fromJson(m_userConfigDisplay.toUtf8());
    m_userConfig = d.object();
    QJsonValue value = m_userConfig.value(QString("researcher"));
    qWarning() << m_userConfig;
//    QJsonObject item = value.toObject();
//    qWarning() << tr("QJsonObject of description: ") << item;

//    /* in case of string value get value and convert into string*/
//    qWarning() << tr("QJsonObject[appName] of description: ") << item["description"];
//    QJsonValue subobj = item["description"];
//    qWarning() << subobj.toString();

//    /* in case of array get array and convert into string*/
//    qWarning() << tr("QJsonObject[appName] of value: ") << item["imp"];
//    QJsonArray test = item["imp"].toArray();
//    qWarning() << test[1].toString();
}
