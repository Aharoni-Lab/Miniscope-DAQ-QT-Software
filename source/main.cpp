#include <QGuiApplication>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>
#include <QObject>
#include <QTreeView>

#include <QThreadPool>

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QQuickStyle>
#include <QStyleHints>

#include "backend.h"
#ifdef Q_OS_MACOS
#include "bundlepaths.h"
#endif

#include <opencv2/core/version.hpp>   // CV_VERSION (e.g. "4.13.0"); macro-only header

#ifdef USE_PYTHON
#include <patchlevel.h>   // PY_VERSION (e.g. "3.12.7"); pure macro header, safe alone
#endif

// The app version comes from the project() line in CMakeLists.txt (passed in
// as MINISCOPE_VERSION) so it cannot drift from the packaged artifact names.
#ifndef MINISCOPE_VERSION
#define MINISCOPE_VERSION "unknown"
#endif
#define VERSION_NUMBER MINISCOPE_VERSION

// For Window's deployment
//C:\Qt\5.12.6>C:\Qt\5.12.6\msvc2017_64\bin\windeployqt.exe --qmldir C:\Users\DBAharoni\Documents\Projects\Miniscope-DAQ-QT-Software\Miniscope-DAQ-QT-Software\ C:\Users\DBAharoni\Documents\Projects\Miniscope-DAQ-QT-Software\build-Miniscope-DAQ-QT-Software-Desktop_Qt_5_12_6_MSVC2017_64bit-Release\release\Miniscope-DAQ-QT-Software.exe
int main(int argc, char *argv[])
{
    // Qt6: high-DPI scaling is always on, so AA_EnableHighDpiScaling is gone
    // (it was a no-op / deprecated).

    // The custom video/trace/tracker renderers issue raw OpenGL commands, so
    // force the scene graph's RHI backend to OpenGL. Qt6 defaults to Direct3D 11
    // on Windows, under which the raw-GL code would not work. This must be called
    // before any QQuickWindow is created.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);

    QGuiApplication app(argc, argv);

    // For QSettings-backed state (theme choice, window layouts).
    QCoreApplication::setOrganizationName("Aharoni Lab");
    QCoreApplication::setApplicationName("Miniscope DAQ");

#ifdef Q_OS_MACOS
    // Packaged .app: switch to a writable working directory holding the
    // app-internal configs and default the user-config/data folders, BEFORE
    // the backend is constructed (it reads ./deviceConfigs in its
    // constructor). No-op for dev builds run from the repo root.
    BundlePaths::prepareBundleRuntime();
#endif

    // Qt6: the default Controls style on Windows is the native style, which does
    // not allow customizing control backgrounds (the QML relies on that). "Basic"
    // is Qt6's renamed, fully-customizable "Default" style from Qt5.
    QQuickStyle::setStyle("Basic");

    // Dark scheme by default, matching the Theme singleton's default; the
    // shell's theme toggle switches both (backEnd::setColorSchemeDark).
    app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    qRegisterMetaType < QVector<quint8> >("QVector<quint8>");

    // Design-token singleton: every color/font/metric in the QML comes from
    // here (import Miniscope.Theme 1.0).
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/Theme.qml")),
                             "Miniscope.Theme", 1, 0, "Theme");

    QQmlApplicationEngine engine;
    // For a deployed (standalone) build, find the bundled QML modules next to
    // the exe. Harmless when running against the conda env. (Qt auto-finds the
    // platform/image plugins in <appdir>/<category>.)
    engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");
    const QUrl url(QStringLiteral("qrc:/AppShell.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

//    qDebug() << "Max Thread:" << QThreadPool().maxThreadCount();

    backEnd backend;
    engine.rootContext()->setContextProperty("backend", &backend);

//    QObject *rootObject = engine.rootObjects().first();
//    QTreeView *qmlObject = engine.rootObjects().first()->findChild<QTreeView*>("treeView");

    engine.load(url);

    backend.setVersionNumber(VERSION_NUMBER);

    // Build/runtime info shown in the Help dialog.
#ifdef USE_PYTHON
    const QString pythonVersion = QStringLiteral(PY_VERSION);
#else
    const QString pythonVersion = QStringLiteral("not built");
#endif
    backend.setBuildInfo(QStringLiteral("Qt %1 | OpenCV %2 | Python %3 | Built %4")
                             .arg(QString::fromLatin1(qVersion()))
                             .arg(QStringLiteral(CV_VERSION))
                             .arg(pythonVersion)
                             .arg(QStringLiteral(__DATE__)));
//    qDebug() << "TTTEEEE" << engine.rootObjects().first()->findChild<QObject*>("treeView");
//    QObject::connect(engine.rootObjects().first()->findChild<QObject*>("treeView"), &QTreeView::clicked, &backend, &backEnd::treeViewclicked);
    QObject::connect(&backend, &backEnd::closeAll, &engine, &QQmlApplicationEngine::quit);
    return app.exec();
}
