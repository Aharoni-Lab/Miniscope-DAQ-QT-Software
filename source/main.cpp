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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>

#include "backend.h"
#include "themecontroller.h"
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
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

// Optional file log, enabled by setting MINISCOPE_LOG_FILE to a path. The app is
// a GUI-subsystem binary on Windows, so Qt's default handler writes to the
// debugger rather than to stderr and a redirected stderr captures nothing -
// which leaves a user reporting a field problem (a device that stops streaming,
// a failed connect) with no log to send. Off unless the variable is set, and it
// still forwards everything to the default handler.
static QtMessageHandler g_previousMessageHandler = nullptr;
// Heap-allocated and deliberately never freed: the handler stays installed for
// the life of the process, so file-scope objects would be destroyed while a
// later-destroyed translation unit could still log through them.
static QFile *g_logFile = nullptr;
static QMutex *g_logMutex = nullptr;

static void fileMessageHandler(QtMsgType type, const QMessageLogContext &context,
                               const QString &msg)
{
    // qFormatLogMessage applies the pattern installed below - or the user's
    // QT_MESSAGE_PATTERN, which still wins - so the file and stderr can never
    // disagree about format, and file/line/function stay available to it.
    const QByteArray line = qFormatLogMessage(type, context, msg).toUtf8() + '\n';
    {
        QMutexLocker locker(g_logMutex);
        if (g_logFile->isOpen()) {
            g_logFile->write(line);
            g_logFile->flush();   // a crash is the case this exists for; never buffer
        }
    }
    if (g_previousMessageHandler)
        g_previousMessageHandler(type, context, msg);
}

static void installFileLogIfRequested()
{
    const QString path = qEnvironmentVariable("MINISCOPE_LOG_FILE");
    if (path.isEmpty())
        return;

    // The thread id earns its place: every device captures on its own thread, so
    // a multi-device stall log is interleaved and otherwise unattributable.
    qSetMessagePattern(QStringLiteral("%{time yyyy-MM-ddTHH:mm:ss.zzz} [%{type}] "
                                      "%{threadid} %{if-category}%{category}: %{endif}"
                                      "%{message}"));

    g_logFile = new QFile(path);
    g_logMutex = new QMutex;
    // A path inside a folder that does not exist yet is the usual way this
    // silently produces nothing, and the warning below is itself unreadable on
    // the Windows GUI-subsystem binary the feature exists for.
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "Could not open log file" << path << "-" << g_logFile->errorString();
        return;
    }
    g_previousMessageHandler = qInstallMessageHandler(fileMessageHandler);
}

#ifdef Q_OS_WIN
// Ask hybrid-GPU (laptop iGPU + discrete) drivers to run this app on the
// discrete GPU. By default the OpenGL scene graph + raw-GL video underlays
// land on the power-saving iGPU, where resizing a session window stalls for
// SECONDS per step inside SwapBuffers: the session holds two nested GL
// swapchains (shell + embedded video pane), and the iGPU/DWM path serializes
// their resizes pathologically (bench: 50-step resize storm 22.6s on Intel
// Arc vs 3.4s on the discrete GPU, with capture starved to ~6 FPS vs a steady
// ~30 FPS). These exported symbols are the documented NVIDIA/AMD opt-ins.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif
int main(int argc, char *argv[])
{
    // Before anything that can log, so startup problems land in the file too.
    installFileLogIfRequested();

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

#if defined(Q_OS_MACOS)
    // Packaged .app: switch to a writable working directory holding the
    // app-internal configs and default the user-config/data folders, BEFORE
    // the backend is constructed (it reads ./deviceConfigs in its
    // constructor). No-op for dev builds run from the repo root.
    BundlePaths::prepareBundleRuntime();
#elif defined(Q_OS_WIN)
    // Packaged Windows build: the launcher has already set the working
    // directory to the install root (holding deviceConfigs/Scripts/userConfigs),
    // so unlike macOS we only need the user-config/data half of the contract:
    // seed the example configs into ~/Documents/Miniscope and point the dialogs
    // there, so the user's own configs and recordings live outside the install
    // dir (which upgrades refresh and uninstall deletes). Detected by the real
    // exe living under .../bin (the deployed layout); a dev build run straight
    // from the build tree keeps the run-from-CWD behavior.
    if (QFileInfo(QCoreApplication::applicationDirPath()).fileName() == QLatin1String("bin"))
        BundlePaths::seedUserDataDirs(QDir::currentPath());
#endif

    // Qt6: the default Controls style on Windows is the native style, which does
    // not allow customizing control backgrounds (the QML relies on that). "Basic"
    // is Qt6's renamed, fully-customizable "Default" style from Qt5.
    QQuickStyle::setStyle("Basic");

    // The OS-level color scheme follows the app theme (ThemeController
    // applies it on registration and on every toggle).
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
        // With MINISCOPE_PANE_TEST also set, the hook pops the first pane
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
                // Drive the first pane through a float -> dock cycle by
                // invoking the pane host's own setFloating (the same path
                // the pane-header button takes), then shoot both states.
                auto setFloating = [&engine, &backend](bool floating) {
                    const QVariantList panes = backend.sessionPanes();
                    if (panes.isEmpty())
                        return;
                    const auto roots = engine.rootObjects();
                    for (QObject *root : roots) {
                        if (QObject *view = root->findChild<QObject *>("acquireView")) {
                            QMetaObject::invokeMethod(view, "setFloating",
                                                      Q_ARG(QVariant, panes.first()),
                                                      Q_ARG(QVariant, floating));
                            return;
                        }
                    }
                };
                QTimer::singleShot(2500, &backend, [setFloating] { setFloating(true); });
                QTimer::singleShot(6500, &backend, [setFloating] { setFloating(false); });
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
