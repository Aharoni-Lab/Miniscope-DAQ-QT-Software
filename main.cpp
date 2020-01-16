#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>
#include <QObject>

#include <QThreadPool>

#include "backend.h"

// TODO: have exit button close everything
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
