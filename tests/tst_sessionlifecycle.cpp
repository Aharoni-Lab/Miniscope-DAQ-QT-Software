// Integration test for the re-entrant session lifecycle:
//   load config -> Run -> endSession -> Run again -> endSession -> exit path
// in ONE process, which is the core invariant behind the ui-v3 shell's
// Setup <-> Acquire mode switching ("change configs without restarting").
//
// Drives the real backEnd with a real (file-playback) video stream so the
// capture thread, ring buffers, device windows, control panel, and DataSaver
// thread all actually start and stop. No camera or hardware needed: the
// device uses the config's videoPlayback source, fed by a tiny AVI generated
// into a temp dir at test start.
//
// Runs on the offscreen platform (see tests/CMakeLists.txt): windows are
// created but never exposed, so no GL context is required.

#include <QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

#include <opencv2/opencv.hpp>

#include "backend.h"
#include "themecontroller.h"

class TestSessionLifecycle : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void runEndRunCycle();

private:
    // Let deferred deletions (endSession tears windows down via deleteLater)
    // and queued cross-thread signals drain.
    void drainEvents(int ms = 200)
    {
        QTest::qWait(ms);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }

    QTemporaryDir m_tempDir;
    QString m_configPath;
};

void TestSessionLifecycle::initTestCase()
{
    // The backend resolves ./deviceConfigs/*.json relative to the CWD; the
    // ctest WORKING_DIRECTORY property points that at the repo root.
    QVERIFY2(QFile::exists("deviceConfigs/videoDevices.json"),
             "test must run with the repo root as its working directory");
    QVERIFY(m_tempDir.isValid());

    // Generate the playback source the device will stream from:
    // <tempdir>/lifecycle_0.avi (VideoStreamOCV opens "<prefix>0.avi").
    // Plenty of frames so the file doesn't run out mid-test.
    const QString videoPath = m_tempDir.filePath("lifecycle_0.avi");
    cv::VideoWriter writer(videoPath.toStdString(),
                           cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                           20, cv::Size(640, 480), true);
    if (!writer.isOpened())
        QSKIP("MJPG codec unavailable on this host; cannot generate the playback video");
    cv::Mat frame(480, 640, CV_8UC3);
    for (int i = 0; i < 600; i++) {
        frame.setTo(cv::Scalar(i % 256, (i * 3) % 256, (i * 7) % 256));
        writer.write(frame);
    }
    writer.release();

    // A minimal, valid config: one behavior camera streaming the generated
    // file. No live camera, no tracker, no trace display, no commutator.
    QJsonObject playback{{"folderPath", m_tempDir.path()},
                         {"filePrefix", "lifecycle_"},
                         {"frameRate", 20}};
    QJsonObject camera{{"deviceType", "WebCam"},
                       {"videoPlayback", playback},
                       {"compression", "MJPG"},
                       {"framesPerFile", 1000},
                       {"windowScale", 0.5},
                       {"windowX", 50},
                       {"windowY", 50}};
    QJsonObject config{
        {"dataDirectory", m_tempDir.path()},
        {"researcherName", "lifecycleTest"},
        {"directoryStructure", QJsonArray{"researcherName", "date", "time"}},
        {"devices", QJsonObject{{"cameras", QJsonObject{{"PlaybackCam", camera}}}}},
    };

    m_configPath = m_tempDir.filePath("lifecycle_config.json");
    QFile f(m_configPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(config).toJson());
    f.close();
}

void TestSessionLifecycle::runEndRunCycle()
{
    backEnd backend;

    if (!backend.availableCodecs().contains("MJPG"))
        QSKIP("MJPG codec unavailable on this host");

    backend.setUserConfigFileName(QUrl::fromLocalFile(m_configPath).toString());
    QVERIFY2(backend.userConfigOK(), "generated config failed the backend's checks");
    QVERIFY(backend.hasDevices());
    QVERIFY(!backend.sessionActive());

    // Run the full shell too (Setup/Acquire stack + the Acquire pane host), so
    // Run / endSession also exercise WindowContainer embed and release against
    // the real window teardown. Offscreen: everything is created, nothing is
    // composited.
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl(QStringLiteral("qrc:/AppShell.qml")));
    QVERIFY2(!engine.rootObjects().isEmpty(), "AppShell.qml failed to load");

    // --- Session 1 -----------------------------------------------------------
    backend.onRunClicked();
    QVERIFY(backend.sessionActive());
    QCOMPARE(backend.sessionCameraCount(), 1);
    QCOMPARE(backend.sessionMiniscopeCount(), 0);

    // Pane descriptors: one camera pane, carrying the CONFIG's device name (a
    // playback device has no deviceID - the old operator[] lookup corrupted
    // its name to "VideoDevice 0"). The session controller is windowless.
    const QVariantList panes = backend.sessionPanes();
    QCOMPARE(panes.size(), 1);
    QCOMPARE(panes[0].toMap().value("name").toString(), QStringLiteral("PlaybackCam"));
    QVERIFY(panes[0].toMap().value("window").value<QObject *>() != nullptr);

    // Session controller: drives a real record/stop round trip (DataSaver
    // threads, file creation, the backend's recording flag).
    QObject *ctl = backend.sessionControl();
    QVERIFY(ctl != nullptr);
    QCOMPARE(ctl->property("recording").toBool(), false);
    QVERIFY(QMetaObject::invokeMethod(ctl, "startRecording"));
    drainEvents(600); // queued cross-thread start + a few saved frames
    QVERIFY(backend.recording());
    QCOMPARE(ctl->property("recording").toBool(), true);
    QVERIFY(QMetaObject::invokeMethod(ctl, "stopRecording"));
    drainEvents(300);
    QVERIFY(!backend.recording());

    // Telemetry snapshot: the device appears with live counters.
    const QVariantMap telemetry = backend.sessionTelemetry();
    QCOMPARE(telemetry.value("devices").toList().size(), 1);
    QVERIFY(telemetry.value("devices").toList()[0].toMap()
                .value("frames").toInt() > 0);
    drainEvents(500); // let the capture + saver threads actually run a bit

    backend.endSession();
    QVERIFY(!backend.sessionActive());
    QCOMPARE(backend.sessionCameraCount(), 0);
    drainEvents();

    // --- Session 2 (same process; this is the new capability) ----------------
    backend.onRunClicked();
    QVERIFY2(backend.sessionActive(), "second Run in the same process failed");
    QCOMPARE(backend.sessionCameraCount(), 1); // exactly one - not doubled
    drainEvents(500);

    // Run while active must be a no-op, not a device duplication.
    backend.onRunClicked();
    QCOMPARE(backend.sessionCameraCount(), 1);

    backend.endSession();
    QVERIFY(!backend.sessionActive());
    QCOMPARE(backend.sessionCameraCount(), 0);
    drainEvents();

    // endSession with no session must be a no-op.
    backend.endSession();
    QVERIFY(!backend.sessionActive());

    // The quit path (endSession + closeAll) must be safe after all the above.
    backend.exitClicked();
    drainEvents();
}

int main(int argc, char *argv[])
{
    // Match the app: the custom video renderers are raw OpenGL, and the
    // controls style must allow customization (see main.cpp).
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QQuickStyle::setStyle("Basic");
    QGuiApplication app(argc, argv);
    // AppShell's Settings element needs these; distinct names keep test runs
    // out of the real app's stored settings (theme, pane layouts).
    QCoreApplication::setOrganizationName("Aharoni Lab Tests");
    QCoreApplication::setApplicationName("Miniscope DAQ Test");
    registerMiniscopeQmlTypes(); // session windows import Miniscope.Theme
    TestSessionLifecycle tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_sessionlifecycle.moc"
