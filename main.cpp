#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>
#include <QObject>

#include <QThreadPool>

#include "backend.h"

// TODO: have exit button close everything

// For Window's deployment
// C:\Qt\5.12.6\mingw73_64\bin>windeployqt.exe --qmldir C:\Users\DBAharoni\Documents\Projects\Miniscope-DAQ-QT-Software\Miniscope-DAQ-QT-Software C:\Users\DBAharoni\Documents\Projects\Miniscope-DAQ-QT-Software\build-Miniscope-DAQ-QT-Software-Desktop_Qt_5_12_6_MinGW_64_bit-Release\release\Miniscope-DAQ-QT-Software.exe
int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QGuiApplication app(argc, argv);

    qRegisterMetaType < QVector<quint8> >("QVector<quint8>");

    QQmlApplicationEngine engine;
    const QUrl url(QStringLiteral("qrc:/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

//    qDebug() << "Max Thread:" << QThreadPool().maxThreadCount();

    backEnd backend;
    engine.rootContext()->setContextProperty("backend", &backend);

    engine.load(url);

    QObject::connect(&backend, &backEnd::closeAll, &engine, &QQmlApplicationEngine::quit);
    return app.exec();
}
