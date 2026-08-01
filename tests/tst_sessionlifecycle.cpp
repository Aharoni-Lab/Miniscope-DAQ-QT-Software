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
#include <QQmlProperty>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QRegularExpression>
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
    void init();
    void runEndRunCycle();
    void configEditsBetweenRunsAreHonored();
    void producersHeldUntilConsumerReady();
    void messageAlertExpires();
    void videoWindowPanelsScaleToPane();
    void paneGridArrangement();

private:
    // Let deferred deletions (endSession tears windows down via deleteLater)
    // and queued cross-thread signals drain.
    void drainEvents(int ms = 200)
    {
        QTest::qWait(ms);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }

    // Writes a config whose only devices are file-playback cameras with the
    // given names, all streaming the AVI generated in initTestCase. Returns its
    // path, or an empty string if it could not be written.
    QString writeConfig(const QString &fileName, const QStringList &cameraNames,
                        int frameRate = 20);

    // Items a Repeater created (every pane of the grid) are not QObject children
    // of the window, so findChildren() can't see them - walk the visual tree.
    static void collectItems(QQuickItem *root, const QString &objectName,
                             QList<QQuickItem *> &out)
    {
        if (!root)
            return;
        const QList<QQuickItem *> kids = root->childItems();
        for (QQuickItem *kid : kids) {
            if (kid->objectName() == objectName)
                out.append(kid);
            collectItems(kid, objectName, out);
        }
    }

    QTemporaryDir m_tempDir;
    QString m_configPath;
};

QString TestSessionLifecycle::writeConfig(const QString &fileName,
                                          const QStringList &cameraNames,
                                          int frameRate)
{
    const QJsonObject playback{{"folderPath", m_tempDir.path()},
                               {"filePrefix", "lifecycle_"},
                               {"frameRate", frameRate}};
    QJsonObject cameras;
    for (const QString &name : cameraNames)
        cameras[name] = QJsonObject{{"deviceType", "WebCam"},
                                    {"videoPlayback", playback},
                                    {"compression", "MJPG"},
                                    {"framesPerFile", 1000},
                                    {"windowScale", 0.5},
                                    {"windowX", 50},
                                    {"windowY", 50}};
    const QJsonObject config{
        {"dataDirectory", m_tempDir.path()},
        {"researcherName", "lifecycleTest"},
        {"directoryStructure", QJsonArray{"researcherName", "date", "time"}},
        {"devices", QJsonObject{{"cameras", cameras}}},
    };

    const QString path = m_tempDir.filePath(fileName);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write(QJsonDocument(config).toJson());
    f.close();
    return path;
}

// Fail on a signal/slot connect that did not resolve. Qt reports those as a
// warning, not an error, so a broken string-based connect() runs forever in
// silence: the dF/F switch was connected on the shared VideoDevice base to a slot
// that only Miniscope declares, and THIS test - which builds a behavior camera
// and drives Run/endSession - printed "No such slot
// BehaviorCam::handleDFFSwitchChange" on every CI run and still passed. QML
// signals have to be connected by name, so no compile-time check is available
// and this is the only thing that can catch the next one.
//
// It must live in init(), not initTestCase(): failOnWarning() only holds for the
// duration of the test function that calls it, and initTestCase() is itself one -
// setting it there silently protects nothing.
void TestSessionLifecycle::init()
{
    QTest::failOnWarning(QRegularExpression("No such (slot|signal)"));
}

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
    m_configPath = writeConfig("lifecycle_config.json", {"PlaybackCam"});
    QVERIFY(!m_configPath.isEmpty());
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
    // Run narrates its progress for the shell's startup overlay; whatever
    // happens, `starting` must not be left true (the Run button and the overlay
    // are gated on it, so a stuck flag locks the UI out permanently).
    QSignalSpy startingSpy(&backend, &backEnd::startingChanged);
    QVERIFY(!backend.starting());
    backend.onRunClicked();
    QVERIFY(backend.sessionActive());
    QVERIFY2(!backend.starting(), "the starting flag was left set after Run");
    QVERIFY(backend.startupStage().isEmpty());
    QCOMPARE(startingSpy.count(), 2);   // on, then off
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

    // The folder the recording actually writes into has to reach the GUI
    // thread - it's what the session bar's "Data folder" button opens, and it
    // is NOT the configured dataDirectory: DataSaver appends the
    // directoryStructure folders (date / time / ...) at record start, on its
    // own thread.
    const QString recordDir = backend.recordDirectory();
    QVERIFY2(!recordDir.isEmpty(), "the record directory never reached the backend");
    QVERIFY2(QFileInfo(recordDir).isDir(), qPrintable(recordDir + " is not a directory"));
    QVERIFY2(recordDir.startsWith(m_tempDir.path()), qPrintable(recordDir));
    QVERIFY2(recordDir != m_tempDir.path(), "record directory skipped directoryStructure");
    // Nothing to open when the path is gone or unset (no file manager is
    // launched here: openDirectory rejects both before handing off to the OS).
    QVERIFY(!backend.openDirectory(recordDir + "/no-such-subfolder"));
    QVERIFY(!backend.openDirectory(QString()));
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
    // A new session has recorded nothing yet: "Data folder" must not still
    // point at the previous session's recording.
    QVERIFY2(backend.recordDirectory().isEmpty(),
             qPrintable("stale record directory: " + backend.recordDirectory()));
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

// What the config says at Run time is what the session must contain - after an
// edit between sessions, and after switching config files.
//
// The parsed device sections (ucMiniscopes / ucBehaviorCams) are backend members
// filled key-by-key rather than assigned, so entries from a previous parse used
// to survive: a device deleted from the config was rebuilt on the next Run (the
// removed webcam reappeared in the Acquire grid), and a different config
// inherited the previous one's devices. Session teardown cannot catch this - it
// clears the constructed device objects, and these maps are what they are built
// FROM.
void TestSessionLifecycle::configEditsBetweenRunsAreHonored()
{
    backEnd backend;
    if (!backend.availableCodecs().contains("MJPG"))
        QSKIP("MJPG codec unavailable on this host");

    const QString twoCams = writeConfig("two_cams.json", {"KeptCam", "RemovedCam"});
    QVERIFY(!twoCams.isEmpty());
    backend.setUserConfigFileName(QUrl::fromLocalFile(twoCams).toString());
    QVERIFY2(backend.userConfigOK(), "two-camera config failed the backend's checks");

    backend.onRunClicked();
    QCOMPARE(backend.sessionCameraCount(), 2);
    QCOMPARE(backend.sessionPanes().size(), 2);
    backend.endSession();
    drainEvents();

    // Delete one device, exactly as the form editor's remove button does.
    backend.removeDevice("cameras", "RemovedCam");
    QVERIFY(backend.userConfigOK());

    backend.onRunClicked();
    QCOMPARE(backend.sessionCameraCount(), 1);
    const QVariantList panes = backend.sessionPanes();
    QCOMPARE(panes.size(), 1);
    QCOMPARE(panes[0].toMap().value("name").toString(), QStringLiteral("KeptCam"));
    backend.endSession();
    drainEvents();

    // Switching to a different config file: only ITS devices may run.
    const QString otherCam = writeConfig("other_cam.json", {"OtherCam"});
    QVERIFY(!otherCam.isEmpty());
    backend.setUserConfigFileName(QUrl::fromLocalFile(otherCam).toString());
    QVERIFY(backend.userConfigOK());

    backend.onRunClicked();
    QCOMPARE(backend.sessionCameraCount(), 1);
    const QVariantList otherPanes = backend.sessionPanes();
    QCOMPARE(otherPanes.size(), 1);
    QCOMPARE(otherPanes[0].toMap().value("name").toString(), QStringLiteral("OtherCam"));
    backend.endSession();
    drainEvents();
}

// A device's ring buffer has exactly one drain: the DataSaver thread. That
// thread cannot be created until every device is, because it is wired to their
// buffers - and constructing a device opens a camera, which takes seconds. So a
// device that streamed from the moment it connected filled all FRAME_BUFFER_SIZE
// slots while the remaining devices were still opening, and Run ended with a
// message log full of "frame buffer is full. Frames will be lost!" before the
// operator had done anything.
//
// Devices are therefore constructed with their capture loop held and released
// together once the DataSaver is up. Three playback cameras at a high frame rate
// reproduce the original flood: each device takes ~0.5 s to construct, which is
// far longer than the first one needs to fill 128 slots.
void TestSessionLifecycle::producersHeldUntilConsumerReady()
{
    backEnd backend;
    if (!backend.availableCodecs().contains("MJPG"))
        QSKIP("MJPG codec unavailable on this host");

    const QString path = writeConfig("held_streams.json",
                                     {"FastCamA", "FastCamB", "FastCamC"},
                                     /*frameRate=*/500);
    QVERIFY(!path.isEmpty());
    backend.setUserConfigFileName(QUrl::fromLocalFile(path).toString());
    QVERIFY2(backend.userConfigOK(), "three-camera config failed the backend's checks");

    backend.onRunClicked();
    QVERIFY(backend.sessionActive());
    QCOMPARE(backend.sessionCameraCount(), 3);

    QObject *ctl = backend.sessionControl();
    QVERIFY(ctl != nullptr);
    const QStringList log = ctl->property("messageLog").toStringList();
    for (const QString &line : log)
        QVERIFY2(!line.contains("buffer is full"),
                 qPrintable("a device streamed before the DataSaver existed: " + line));

    // ...and the hold really was released, rather than left on forever: every
    // device has to be acquiring frames now.
    drainEvents(400);
    const QVariantList devices = backend.sessionTelemetry().value("devices").toList();
    QCOMPARE(devices.size(), 3);
    for (const QVariant &device : devices) {
        const QVariantMap map = device.toMap();
        QVERIFY2(map.value("frames").toInt() > 0,
                 qPrintable(map.value("name").toString() + " never acquired a frame"));
    }

    backend.endSession();
    QVERIFY(!backend.sessionActive());
    drainEvents();
}

// The message card's colored outline is an alert that expires, not a state
// derived from the session's cumulative counts. Driven off the counts, a single
// benign warning - a commutator reporting no rotation from its first samples,
// with the commutator working perfectly - ringed the card amber for the rest of
// the session, which reads as an unresolved fault. The counts in the header are
// the persistent record; the ring is only the attention-grab.
void TestSessionLifecycle::messageAlertExpires()
{
    backEnd backend;
    if (!backend.availableCodecs().contains("MJPG"))
        QSKIP("MJPG codec unavailable on this host");

    backend.setUserConfigFileName(QUrl::fromLocalFile(m_configPath).toString());
    QVERIFY(backend.userConfigOK());

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl(QStringLiteral("qrc:/AppShell.qml")));
    QVERIFY2(!engine.rootObjects().isEmpty(), "AppShell.qml failed to load");

    backend.onRunClicked();
    QVERIFY(backend.sessionActive());
    drainEvents();

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window != nullptr);
    QList<QQuickItem *> bars;
    collectItems(window->contentItem(), QStringLiteral("sessionBar"), bars);
    QCOMPARE(bars.size(), 1);
    QQuickItem *bar = bars.first();
    QList<QQuickItem *> cards;
    collectItems(bar, QStringLiteral("sessionMessages"), cards);
    QCOMPARE(cards.size(), 1);
    QQuickItem *card = cards.first();

    // Normalize first: Run itself logs a few (severity-0) lines, and this test is
    // about what happens to the outline afterwards. border.color is a grouped
    // property, so it has to be read through QQmlProperty, not QObject::property.
    QVERIFY(QMetaObject::invokeMethod(bar, "expireAlert"));
    drainEvents(600);
    const QColor quiet = QQmlProperty::read(card, "border.color").value<QColor>();
    const int errorsBefore = bar->property("errorCount").toInt();
    const int warningsBefore = bar->property("warningCount").toInt();

    QObject *ctl = backend.sessionControl();
    QVERIFY(ctl != nullptr);

    QVERIFY(QMetaObject::invokeMethod(ctl, "receiveMessage",
                                      Q_ARG(QString, "Warning: commutator computed no rotation")));
    drainEvents(100);
    QCOMPARE(bar->property("alertSeverity").toInt(), 1);
    const QColor alerted = QQmlProperty::read(card, "border.color").value<QColor>();
    QVERIFY2(alerted != quiet, "a new warning did not raise the outline");
    QCOMPARE(bar->property("warningCount").toInt(), warningsBefore + 1);

    // An error outranks a warning that is still showing...
    QVERIFY(QMetaObject::invokeMethod(ctl, "receiveMessage",
                                      Q_ARG(QString, "ERROR: something real")));
    drainEvents(100);
    QCOMPARE(bar->property("alertSeverity").toInt(), 2);
    // ...and a later warning must not downgrade it while it is up.
    QVERIFY(QMetaObject::invokeMethod(ctl, "receiveMessage",
                                      Q_ARG(QString, "Warning: and another")));
    drainEvents(100);
    QCOMPARE(bar->property("alertSeverity").toInt(), 2);

    // When the alert expires (alertTimer fires this), the ring goes back to
    // normal but the session's counts - the thing that must not be forgotten -
    // are all still there.
    QVERIFY(QMetaObject::invokeMethod(bar, "expireAlert"));
    drainEvents(600);   // the Behavior animates the colour back (4 * animMs)
    QCOMPARE(bar->property("alertSeverity").toInt(), 0);
    QCOMPARE(QQmlProperty::read(card, "border.color").value<QColor>(), quiet);
    QCOMPARE(bar->property("errorCount").toInt(), errorsBefore + 1);
    QCOMPARE(bar->property("warningCount").toInt(), warningsBefore + 2);

    backend.endSession();
    drainEvents();
}

// The hardware dock and the display rail overlay the live video, and most of
// what they hold (EWL focus, LED, gain, contrast) is adjusted *while watching*
// that video. At a fixed 250 px each they covered 40-56% of a normal Acquire pane
// individually, and effectively all of it together. So they are a fraction of the
// pane, and only one is ever out at a time.
void TestSessionLifecycle::videoWindowPanelsScaleToPane()
{
    backEnd backend;
    if (!backend.availableCodecs().contains("MJPG"))
        QSKIP("MJPG codec unavailable on this host");

    backend.setUserConfigFileName(QUrl::fromLocalFile(m_configPath).toString());
    QVERIFY(backend.userConfigOK());
    backend.onRunClicked();
    QVERIFY(backend.sessionActive());

    const QVariantList panes = backend.sessionPanes();
    QCOMPARE(panes.size(), 1);
    auto *view = qobject_cast<QQuickView *>(
        panes[0].toMap().value("window").value<QObject *>());
    QVERIFY2(view != nullptr, "the device pane is not a QQuickView");
    QQuickItem *shell = view->rootObject();
    QVERIFY(shell != nullptr);
    QQuickItem *rail = shell->findChild<QQuickItem *>(QStringLiteral("displayRail"));
    QQuickItem *dock = shell->findChild<QQuickItem *>(QStringLiteral("hardwareDock"));
    QVERIFY(rail != nullptr);
    QVERIFY(dock != nullptr);

    // Width tracks the pane: 34% of it, floored where a slider row stops being
    // usable and capped at the old fixed width. The shell item is driven directly
    // rather than through the view - a pane embeds this window in a container that
    // owns its geometry, so setWidth() on the view does not stick.
    shell->setWidth(1200);
    drainEvents(100);
    QCOMPARE(rail->width(), 250.0);           // capped
    shell->setWidth(600);
    drainEvents(100);
    QCOMPARE(rail->width(), 204.0);           // 0.34 * 600
    shell->setWidth(300);
    drainEvents(100);
    QCOMPARE(rail->width(), 150.0);           // floored

    // Not full height either: the rail's controls come to well under a pane.
    shell->setWidth(600);
    shell->setHeight(900);
    drainEvents(100);
    QVERIFY2(rail->height() < 0.9 * shell->height(),
             qPrintable(QStringLiteral("rail is %1 tall in a %2 pane")
                            .arg(rail->height()).arg(shell->height())));

    // One panel at a time: an open rail collapses the dock even when the dock is
    // pinned, so the two can never cover the video together.
    QQmlProperty::write(dock, "hwPinned", true);
    drainEvents(100);
    QCOMPARE(QQmlProperty::read(dock, "hwExpanded").toBool(), true);
    QQmlProperty::write(rail, "railPinned", true);
    drainEvents(100);
    QCOMPARE(QQmlProperty::read(dock, "hwExpanded").toBool(), false);
    QQmlProperty::write(rail, "railPinned", false);
    drainEvents(100);
    QCOMPARE(QQmlProperty::read(dock, "hwExpanded").toBool(), true);

    backend.endSession();
    drainEvents();
}

// The Acquire grid is an editable arrangement: rows of panes the operator can
// swap around and resize, with the row shape and the divider positions stored
// per config file. This drives the arrangement side of that (the divider
// dragging is SplitView's own) and pins the invariant that matters most: every
// pane of the running session appears in the grid exactly once, whatever the
// stored layout says - a config edited between runs must not leave a hole in
// the grid or strand a device outside it.
void TestSessionLifecycle::paneGridArrangement()
{
    backEnd backend;
    if (!backend.availableCodecs().contains("MJPG"))
        QSKIP("MJPG codec unavailable on this host");

    const QString threeCams = writeConfig("grid.json", {"CamA", "CamB", "CamC"});
    QVERIFY(!threeCams.isEmpty());
    backend.setUserConfigFileName(QUrl::fromLocalFile(threeCams).toString());
    QVERIFY2(backend.userConfigOK(), "three-camera config failed the backend's checks");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl(QStringLiteral("qrc:/AppShell.qml")));
    QVERIFY2(!engine.rootObjects().isEmpty(), "AppShell.qml failed to load");

    backend.onRunClicked();
    QCOMPARE(backend.sessionCameraCount(), 3);
    drainEvents(300);

    QObject *grid = nullptr;
    const auto roots = engine.rootObjects();
    for (QObject *root : roots) {
        grid = root->findChild<QObject *>("acquireView");
        if (grid)
            break;
    }
    QVERIFY2(grid, "acquireView missing from the shell");

    // rows is [[name, ...], ...]; flatten for the "exactly once" checks.
    const auto rows = [grid] { return grid->property("rows").toList(); };
    const auto flatten = [](const QVariantList &rowList) {
        QStringList out;
        for (const QVariant &row : rowList)
            out += row.toStringList();
        return out;
    };
    const auto invoke = [grid](const char *fn, const QVariant &a = QVariant(),
                               const QVariant &b = QVariant()) {
        if (!a.isValid())
            return QMetaObject::invokeMethod(grid, fn);
        if (!b.isValid())
            return QMetaObject::invokeMethod(grid, fn, Q_ARG(QVariant, a));
        return QMetaObject::invokeMethod(grid, fn, Q_ARG(QVariant, a), Q_ARG(QVariant, b));
    };

    // Auto columns for three panes is 2, so: a row of two and a row of one.
    QVariantList r = rows();
    QCOMPARE(r.size(), 2);
    QCOMPARE(r.at(0).toStringList().size(), 2);
    QCOMPARE(r.at(1).toStringList().size(), 1);
    QStringList flat = flatten(r);
    QCOMPARE(flat.size(), 3);
    for (const QString &name : {"CamA", "CamB", "CamC"})
        QVERIFY2(flat.contains(name), qPrintable(name + QStringLiteral(" missing from the grid")));

    // A REAL header drag through the window: press a pane's header, move past
    // the drag threshold, release over another pane. This is the whole path -
    // ghost, DropArea, drop delivery - and it is where the first version broke:
    // setting Drag.active back to false on release CANCELS a drag, so the drop
    // never arrived and panes never moved, while the drag itself looked fine.
    {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY2(window, "the shell root is not a window");
        QList<QQuickItem *> frames;
        collectItems(window->contentItem(), QStringLiteral("paneFrame"), frames);
        QCOMPARE(frames.size(), 3);

        QHash<QString, QQuickItem *> frameByName;
        for (QQuickItem *frame : frames)
            frameByName.insert(frame->property("modelData").toString(), frame);

        // Header midpoints, left of the pop-out button that overlays the right
        // end of the header.
        const auto headerPoint = [](QQuickItem *frame) {
            QList<QQuickItem *> headers;
            collectItems(frame, QStringLiteral("paneHeader"), headers);
            if (headers.isEmpty())
                return QPoint();
            QQuickItem *header = headers.first();
            return header->mapToScene(QPointF(header->width() * 0.4,
                                              header->height() / 2)).toPoint();
        };
        const QString fromName = flat.first();
        const QString toName = flat.last();
        QVERIFY(frameByName.contains(fromName) && frameByName.contains(toName));
        const QPoint from = headerPoint(frameByName.value(fromName));
        const QPoint to = headerPoint(frameByName.value(toName));
        QVERIFY(!from.isNull() && !to.isNull());
        QVERIFY2(from != to, "the two panes' headers are at the same place");

        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        // Past the 8 px threshold, then onto the target.
        QTest::mouseMove(window, from + QPoint(20, 0));
        QTest::mouseMove(window, (from + to) / 2);
        QTest::mouseMove(window, to);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        drainEvents(200);

        flat = flatten(rows());
        QCOMPARE(flat.size(), 3);
        QCOMPARE(flat.indexOf(toName), 0);
        QCOMPARE(flat.indexOf(fromName), 2);
    }

    // The same swap driven directly (the function the drop calls) - it never
    // duplicates or drops a pane.
    flat = flatten(rows());
    const QString first = flat.first();
    const QString last = flat.last();
    QVERIFY(invoke("swapPanes", first, last));
    flat = flatten(rows());
    QCOMPARE(flat.size(), 3);
    QCOMPARE(flat.first(), last);
    QCOMPARE(flat.last(), first);

    // Dropping on a divider places the pane in that slot instead of trading
    // places with a neighbour. Rows are [[a, b], [c]] at this point.
    const auto movePaneTo = [grid](const QString &name, int row, int position, bool asNewRow) {
        return QMetaObject::invokeMethod(grid, "movePaneTo",
                                         Q_ARG(QVariant, name), Q_ARG(QVariant, row),
                                         Q_ARG(QVariant, position),
                                         Q_ARG(QVariant, asNewRow));
    };
    r = rows();
    QCOMPARE(r.size(), 2);
    const QString a = r.at(0).toStringList().at(0);
    const QString b = r.at(0).toStringList().at(1);
    const QString c = r.at(1).toStringList().at(0);

    // On the divider between a and b: it lands between them, and the row it
    // came from - now empty - disappears.
    QVERIFY(movePaneTo(c, 0, 1, false));
    r = rows();
    QCOMPARE(r.size(), 1);
    QCOMPARE(r.at(0).toStringList(), QStringList({a, c, b}));

    // On the divider between two rows: a row of its own, right there.
    QVERIFY(movePaneTo(a, 1, 0, true));
    r = rows();
    QCOMPARE(r.size(), 2);
    QCOMPARE(r.at(0).toStringList(), QStringList({c, b}));
    QCOMPARE(r.at(1).toStringList(), QStringList({a}));

    // A divider the pane already borders leaves the grid alone.
    QVERIFY(movePaneTo(c, 0, 0, false));
    QVERIFY(movePaneTo(c, 0, 1, false));
    r = rows();
    QCOMPARE(r.size(), 2);
    QCOMPARE(r.at(0).toStringList(), QStringList({c, b}));

    // The row's outer edges, which have no divider of their own: position 0 is
    // the left edge (before every pane), position == length is the right edge.
    // Rows are [[c, b], [a]] here.
    QVERIFY(movePaneTo(b, 0, 0, false));           // b to the left edge of its row
    QCOMPARE(rows().at(0).toStringList(), QStringList({b, c}));
    QVERIFY(movePaneTo(a, 0, 2, false));           // a to the right edge of that row
    r = rows();
    QCOMPARE(r.size(), 1);                         // a's own row is gone
    QCOMPARE(r.at(0).toStringList(), QStringList({b, c, a}));
    QVERIFY(movePaneTo(a, 0, 0, false));           // and back to the left edge
    QCOMPARE(rows().at(0).toStringList(), QStringList({a, b, c}));

    // A pane's top / bottom edge makes a new row there - the only way to get a
    // second row by dragging when the grid has just one.
    QVERIFY(movePaneTo(c, 0, 0, true));            // c above the single row
    r = rows();
    QCOMPARE(r.size(), 2);
    QCOMPARE(r.at(0).toStringList(), QStringList({c}));
    QCOMPARE(r.at(1).toStringList(), QStringList({a, b}));
    QVERIFY(movePaneTo(a, 2, 0, true));            // a below the last row
    r = rows();
    QCOMPARE(r.size(), 3);
    QCOMPARE(r.at(0).toStringList(), QStringList({c}));
    QCOMPARE(r.at(1).toStringList(), QStringList({b}));
    QCOMPARE(r.at(2).toStringList(), QStringList({a}));
    // A pane already alone in its own row can't be "moved" to either side of it.
    QVERIFY(movePaneTo(c, 0, 0, true));
    QVERIFY(movePaneTo(c, 1, 0, true));
    QCOMPARE(rows().size(), 3);
    QCOMPARE(rows().at(0).toStringList(), QStringList({c}));

    // A fixed column count reshapes the grid: one pane per row.
    QVERIFY(invoke("setColumns", 1));
    r = rows();
    QCOMPARE(r.size(), 3);
    for (const QVariant &row : r)
        QCOMPARE(row.toStringList().size(), 1);

    // ...and both the shape and the column choice are stored for this config.
    QVariantMap meta = backend.paneLayout(QStringLiteral("__layout"));
    QCOMPARE(meta.value(QStringLiteral("columns")).toInt(), 1);
    QVERIFY(meta.value(QStringLiteral("rows")).toString().contains(QStringLiteral("CamA")));

    // Reset returns to the automatic grid.
    QVERIFY(invoke("resetLayout"));
    QCOMPARE(rows().size(), 2);
    QCOMPARE(backend.paneLayout(QStringLiteral("__layout"))
                 .value(QStringLiteral("columns")).toInt(), 0);

    // A device removed from the config between runs must leave the grid, with
    // no empty row left where it was.
    backend.endSession();
    drainEvents();
    backend.removeDevice(QStringLiteral("cameras"), QStringLiteral("CamB"));
    QVERIFY(backend.userConfigOK());
    backend.onRunClicked();
    QCOMPARE(backend.sessionCameraCount(), 2);
    drainEvents(300);

    r = rows();
    flat = flatten(r);
    QCOMPARE(flat.size(), 2);
    QVERIFY(!flat.contains(QStringLiteral("CamB")));
    QVERIFY(flat.contains(QStringLiteral("CamA")));
    QVERIFY(flat.contains(QStringLiteral("CamC")));
    for (const QVariant &row : r)
        QVERIFY2(!row.toStringList().isEmpty(), "the removed device left an empty row");

    backend.endSession();
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
