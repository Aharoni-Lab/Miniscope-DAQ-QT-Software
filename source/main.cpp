#include <QGuiApplication>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>
#include <QObject>
#include <QTreeView>

#include <QThreadPool>
#include <QTimer>

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QQuickStyle>
#include <QStyleHints>

#include "backend.h"
#include "themecontroller.h"
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

    // Keep the OS-level scheme LIGHT for now: the legacy session windows
    // (the device/trace/tracker windows) rely on light
    // palette text over hardcoded light backgrounds, so a dark palette makes
    // them unreadable. The new shell doesn't care - all its colors come from
    // the Theme singleton, so it renders dark regardless. Flip this to follow
    // Theme.dark once the legacy windows are replaced.
    app.styleHints()->setColorScheme(Qt::ColorScheme::Light);

    qRegisterMetaType < QVector<quint8> >("QVector<quint8>");

    // Register Miniscope.Theme (the shared ThemeState + the Theme.qml token
    // singleton) before any engine loads QML that imports it.
    registerMiniscopeQmlTypes();

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

    // Development hook: load the given config and Run immediately, so the full
    // Acquire path can be exercised (and screenshotted) without any clicking.
    const QString autorunConfig = qEnvironmentVariable("MINISCOPE_AUTORUN_CONFIG");
    if (!autorunConfig.isEmpty()) {
        backend.setUserConfigFileName(QUrl::fromLocalFile(autorunConfig).toString());
        if (backend.userConfigOK())
            backend.onRunClicked();
        else
            qWarning() << "MINISCOPE_AUTORUN_CONFIG: config failed checks, not running:"
                       << autorunConfig;

        // Companion hook: after the session has run a few seconds, grab every
        // visible Quick window (shell + panes, GL underlays included) to PNGs
        // in the given directory, then quit. For automated visual checks.
        // With MINISCOPE_PANE_TEST also set, AcquireView pops the first pane
        // out at 2.5s and docks it at 6.5s; shots are taken in both states.
        const QString shotDir = qEnvironmentVariable("MINISCOPE_AUTORUN_SHOT_DIR");
        if (!shotDir.isEmpty()) {
            auto grabAll = [shotDir](const QString &tag) {
                int i = 0;
                const auto windows = QGuiApplication::allWindows();
                for (QWindow *w : windows) {
                    auto *qw = qobject_cast<QQuickWindow *>(w);
                    if (!qw)
                        continue;
                    qInfo() << "panewin" << tag << qw->title()
                            << "visible" << qw->isVisible()
                            << "parent" << (void *)qw->parent()
                            << "flags" << qw->flags()
                            << "geom" << qw->geometry();
                    if (!qw->isVisible())
                        continue;
                    const QString title = qw->title().isEmpty() ? QStringLiteral("pane")
                                                                : qw->title();
                    qw->grabWindow().save(QStringLiteral("%1/%2_win%3_%4.png")
                                              .arg(shotDir, tag).arg(i++).arg(title));
                }
            };
            if (qEnvironmentVariableIsSet("MINISCOPE_PANE_TEST")) {
                QTimer::singleShot(5000, &backend, [grabAll] { grabAll("floating"); });
                QTimer::singleShot(9000, &backend, [grabAll] {
                    grabAll("docked");
                    QCoreApplication::quit();
                });
            } else {
                QTimer::singleShot(6000, &backend, [grabAll] {
                    grabAll("shot");
                    QCoreApplication::quit();
                });
            }
        }
    }

    return app.exec();
}
