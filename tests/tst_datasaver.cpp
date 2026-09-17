// Regression tests for DataSaver configuration handling:
//  - setHeadOrientationConfig once assigned the filter flag over the enable
//    flag (copy-paste bug), silently suppressing the headOrientation.csv
//    whenever filterBadData was false.
//  - The reserved "date"/"time" directoryStructure tokens were matched
//    case-sensitively, so a capitalized config entry ("Date") produced a
//    literal "DateMissing" folder instead of the date.

//  - startRecording() ignored every QFile::open in the save path, so an
//    unwritable data directory produced a UI stuck on "Recording" while
//    nothing was saved. It must fail loudly (recordingFailed) instead.

#include <QtTest/QtTest>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include "datasaver.h"

class TestDataSaver : public QObject
{
    Q_OBJECT

private slots:
    void headOrientationEnableSurvivesFilterOff()
    {
        // The exact bug: enable=true + filter=false must leave enable TRUE.
        DataSaver saver;
        saver.setHeadOrientationConfig("Miniscope", true, false);
        QCOMPARE(saver.getHeadOrientationStreamState("Miniscope"), true);
        QCOMPARE(saver.getHeadOrientationFilterState("Miniscope"), false);
    }

    void headOrientationFlagsAreIndependent()
    {
        DataSaver saver;
        saver.setHeadOrientationConfig("A", true, true);
        saver.setHeadOrientationConfig("B", false, true);
        QCOMPARE(saver.getHeadOrientationStreamState("A"), true);
        QCOMPARE(saver.getHeadOrientationFilterState("A"), true);
        QCOMPARE(saver.getHeadOrientationStreamState("B"), false);
        QCOMPARE(saver.getHeadOrientationFilterState("B"), true);
    }

    void directoryTokensLowerCase()
    {
        QCOMPARE(baseDirFor({"researcherName", "date", "time"}),
                 QStringLiteral("/data/Test_Person/2026_07_23/13_04_05"));
    }

    void directoryTokensAnyCase()
    {
        QCOMPARE(baseDirFor({"researcherName", "Date", "TIME"}),
                 QStringLiteral("/data/Test_Person/2026_07_23/13_04_05"));
    }

    void unknownTokenStillGetsPlaceholder()
    {
        // Non-reserved tokens without a config value keep the documented
        // "<token>Missing" placeholder behavior.
        QCOMPARE(baseDirFor({"animalName", "date"}),
                 QStringLiteral("/data/animalNameMissing/2026_07_23"));
    }

    void startRecordingSucceedsInWritableDir()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        DataSaver saver;
        saver.setUserConfig(deviceFreeConfig(dir.path()));
        QSignalSpy failed(&saver, &DataSaver::recordingFailed);

        saver.startRecording({});
        QVERIFY(saver.isRecording());
        QCOMPARE(failed.count(), 0);
        QVERIFY(QFile::exists(dir.path() + "/notes.csv"));
        QVERIFY(QFile::exists(dir.path() + "/metaData.json"));
        saver.stopRecording();
    }

    void startRecordingFailsWhenBaseDirUncreatable()
    {
        // dataDirectory routed THROUGH an existing file: mkpath must fail.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile blocker(dir.path() + "/blocker");
        QVERIFY(blocker.open(QFile::WriteOnly));
        blocker.close();

        DataSaver saver;
        saver.setUserConfig(deviceFreeConfig(dir.path() + "/blocker/sub"));
        QSignalSpy failed(&saver, &DataSaver::recordingFailed);

        saver.startRecording({});
        QVERIFY(!saver.isRecording());
        QCOMPARE(failed.count(), 1);
    }

    void startRecordingFailsWhenDeviceCsvUnwritable()
    {
        // A FILE occupies the device-directory name, so the per-device mkdir
        // fails and timeStamps.csv cannot be created. Recording must not
        // claim to start.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile blocker(dir.path() + "/Cam");
        QVERIFY(blocker.open(QFile::WriteOnly));
        blocker.close();

        QJsonObject config = deviceFreeConfig(dir.path());
        QJsonObject camera;
        camera["deviceType"] = "WebCam";
        QJsonObject cameras;
        cameras["Cam"] = camera;
        QJsonObject devices;
        devices["cameras"] = cameras;
        config["devices"] = devices;

        DataSaver saver;
        saver.setUserConfig(config);

        // Register the frame source the way backEnd does, so startRecording
        // walks the timeStamps.csv setup for this device.
        cv::Mat frames[1];
        qint64 timestamps[1] = {0};
        QSemaphore freeFrames(1), usedFrames;
        QAtomicInt acqFrame;
        saver.setFrameBufferParameters("Cam", frames, timestamps, nullptr, nullptr, 1,
                                       &freeFrames, &usedFrames, &acqFrame);

        QSignalSpy failed(&saver, &DataSaver::recordingFailed);
        saver.startRecording({});
        QVERIFY(!saver.isRecording());
        QCOMPARE(failed.count(), 1);
    }

    void stopRecordingDrainsBufferAndLogsDaqColumn()
    {
        // Three frames sit acquired-but-unsaved in the ring buffer when the
        // recording stops. stopRecording() must drain them to disk, and the
        // timeStamps.csv of a device WITH a DAQ counter must carry the
        // per-frame "DAQ Frame Number" column (a jump in it is the on-disk
        // evidence of USB frame loss).
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QJsonObject config = deviceFreeConfig(dir.path());
        QJsonObject camera;
        camera["deviceType"] = "Miniscope";
        QJsonObject miniscopes;
        miniscopes["Scope"] = camera;
        QJsonObject devices;
        devices["miniscopes"] = miniscopes;
        config["devices"] = devices;

        DataSaver saver;
        saver.setUserConfig(config);
        saver.setDataCompression("Scope", "MJPG");

        const int bufSize = 4;
        cv::Mat frames[bufSize];
        qint64 timestamps[bufSize] = {0};
        qint64 daqNums[bufSize] = {0};
        QSemaphore freeFrames(bufSize), usedFrames;
        QAtomicInt acqFrame;
        saver.setFrameBufferParameters("Scope", frames, timestamps, nullptr, daqNums,
                                       bufSize, &freeFrames, &usedFrames, &acqFrame);

        saver.startRecording({});
        QVERIFY(saver.isRecording());

        // Simulate the capture backend: fill 3 slots, DAQ counter shows a
        // dropped hardware frame between the 2nd and 3rd (6 -> 8).
        const qint64 daqValues[3] = {5, 6, 8};
        for (int i = 0; i < 3; i++) {
            QVERIFY(freeFrames.tryAcquire());
            frames[i] = cv::Mat(48, 64, CV_8UC3, cv::Scalar(i * 40, 0, 0));
            timestamps[i] = 1000 + i * 33;
            daqNums[i] = daqValues[i];
            usedFrames.release();
        }

        saver.stopRecording();
        QVERIFY(!saver.isRecording());

        QFile csv(dir.path() + "/Scope/timeStamps.csv");
        QVERIFY(csv.open(QFile::ReadOnly | QFile::Text));
        const QStringList lines = QString::fromUtf8(csv.readAll())
                                      .split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 4); // header + 3 drained frames
        QCOMPARE(lines[0], QStringLiteral("Frame Number,Time Stamp (ms),Buffer Index,"
                                         "DAQ Frame Number,Unix Time Stamp (ms)"));
        for (int i = 0; i < 3; i++) {
            const QStringList cols = lines[i + 1].split(',');
            QCOMPARE(cols.size(), 5);
            QCOMPARE(cols[0].toInt(), i);
            QCOMPARE(cols[3].toLongLong(), daqValues[i]);
        }

        // The drained frames must be in the video file, too.
        cv::VideoCapture readBack((dir.path() + "/Scope/0.avi").toStdString());
        QVERIFY(readBack.isOpened());
        QCOMPARE(int(readBack.get(cv::CAP_PROP_FRAME_COUNT)), 3);
    }

    void devicesWithoutDaqCounterOmitTheDaqColumn()
    {
        // Behavior webcams have no DAQ counter (nullptr buffer): their CSV
        // must skip that column and go straight to the UNIX stamp.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QJsonObject config = deviceFreeConfig(dir.path());
        QJsonObject camera;
        camera["deviceType"] = "WebCam";
        QJsonObject cameras;
        cameras["Cam"] = camera;
        QJsonObject devices;
        devices["cameras"] = cameras;
        config["devices"] = devices;

        DataSaver saver;
        saver.setUserConfig(config);
        saver.setDataCompression("Cam", "MJPG");

        cv::Mat frames[2];
        qint64 timestamps[2] = {0};
        QSemaphore freeFrames(2), usedFrames;
        QAtomicInt acqFrame;
        saver.setFrameBufferParameters("Cam", frames, timestamps, nullptr, nullptr,
                                       2, &freeFrames, &usedFrames, &acqFrame);

        saver.startRecording({});
        QVERIFY(saver.isRecording());
        saver.stopRecording();

        QFile csv(dir.path() + "/Cam/timeStamps.csv");
        QVERIFY(csv.open(QFile::ReadOnly | QFile::Text));
        const QString header = QString::fromUtf8(csv.readLine()).trimmed();
        QCOMPARE(header, QStringLiteral("Frame Number,Time Stamp (ms),Buffer Index,"
                                        "Unix Time Stamp (ms)"));
    }

    void unixColumnIsAnchoredToRecordStart()
    {
        // Every row carries absolute UTC time as well as time-since-start.
        // The two are tied by a single anchor taken when recording began, so
        // (unix - relative) must be the SAME value on every row - that is what
        // keeps an NTP step mid-recording from distorting frame intervals - and
        // it must equal the wall clock at the moment recording started.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QJsonObject config = deviceFreeConfig(dir.path());
        QJsonObject camera;
        camera["deviceType"] = "WebCam";
        QJsonObject cameras;
        cameras["Cam"] = camera;
        QJsonObject devices;
        devices["cameras"] = cameras;
        config["devices"] = devices;

        DataSaver saver;
        saver.setUserConfig(config);
        saver.setDataCompression("Cam", "MJPG");

        const int bufSize = 4;
        cv::Mat frames[bufSize];
        qint64 timestamps[bufSize] = {0};
        QSemaphore freeFrames(bufSize), usedFrames;
        QAtomicInt acqFrame;
        saver.setFrameBufferParameters("Cam", frames, timestamps, nullptr, nullptr,
                                       bufSize, &freeFrames, &usedFrames, &acqFrame);

        // Bracket the anchor: whatever startRecording() recorded as "now" has
        // to fall inside this window.
        const qint64 epochBefore = QDateTime::currentMSecsSinceEpoch();
        saver.startRecording({});
        const qint64 epochAfter = QDateTime::currentMSecsSinceEpoch();
        QVERIFY(saver.isRecording());

        // Frames 33 ms apart on the monotonic clock, as the capture thread
        // would deliver them.
        for (int i = 0; i < 3; i++) {
            QVERIFY(freeFrames.tryAcquire());
            frames[i] = cv::Mat(48, 64, CV_8UC3, cv::Scalar(i * 40, 0, 0));
            timestamps[i] = 1000 + i * 33;
            usedFrames.release();
        }

        saver.stopRecording();

        QFile csv(dir.path() + "/Cam/timeStamps.csv");
        QVERIFY(csv.open(QFile::ReadOnly | QFile::Text));
        const QStringList lines = QString::fromUtf8(csv.readAll())
                                      .split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 4); // header + 3 frames

        qint64 anchorMs = 0;
        qint64 previousUnixMs = 0;
        for (int i = 0; i < 3; i++) {
            const QStringList cols = lines[i + 1].split(',');
            QCOMPARE(cols.size(), 4); // no DAQ column on a webcam
            const qint64 relativeMs = cols[1].toLongLong();
            const qint64 unixMs = cols[3].toLongLong();

            if (i == 0)
                anchorMs = unixMs - relativeMs;
            else
                QCOMPARE(unixMs - relativeMs, anchorMs); // one anchor, no re-sync

            // 33 ms of monotonic spacing must survive into absolute time.
            if (i > 0)
                QCOMPARE(unixMs - previousUnixMs, qint64(33));
            previousUnixMs = unixMs;
        }
        QVERIFY(anchorMs >= epochBefore);
        QVERIFY(anchorMs <= epochAfter);

        // metaData.json must agree with the CSV about the anchor, and must
        // carry the closing pair that makes clock drift recoverable.
        QFile meta(dir.path() + "/metaData.json");
        QVERIFY(meta.open(QFile::ReadOnly));
        const QJsonObject metaObj = QJsonDocument::fromJson(meta.readAll()).object();

        const QJsonObject startObj = metaObj.value("recordingStartTime").toObject();
        QCOMPARE(startObj.value("msecSinceEpoch").toVariant().toLongLong(), anchorMs);

        QVERIFY(metaObj.contains("recordingEndTime"));
        const QJsonObject endObj = metaObj.value("recordingEndTime").toObject();
        QVERIFY(endObj.value("msecSinceEpoch").toVariant().toLongLong() >= anchorMs);
        QVERIFY(endObj.value("elapsedMonotonicMs").toVariant().toLongLong() >= 0);
        // The two clocks cannot have diverged meaningfully over a test that
        // lasts milliseconds.
        QVERIFY(qAbs(endObj.value("driftMs").toVariant().toLongLong()) < 1000);
    }

    void stopRunningExitsRunLoop()
    {
        // startRunning() blocks its thread in the save loop; stopRunning()
        // (delivered through the loop's processEvents) must let it return so
        // the thread can be joined. Before R3 there was NO exit path: the
        // loop ran (and busy-spun a core) until process death.
        QThread thread;
        DataSaver saver;
        saver.moveToThread(&thread);
        connect(&thread, &QThread::started, &saver, &DataSaver::startRunning);
        thread.start();
        QTest::qWait(100); // let the loop spin up

        QMetaObject::invokeMethod(&saver, "stopRunning", Qt::QueuedConnection);
        thread.quit();
        QVERIFY(thread.wait(3000));
        saver.moveToThread(QThread::currentThread());
    }

    void backToBackRecordingsReuseCleanly()
    {
        // Each record/stop cycle allocates fresh files/writers; the previous
        // cycle's must be released, not leaked, and the second cycle must
        // work end to end.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        DataSaver saver;
        saver.setUserConfig(deviceFreeConfig(dir.path()));
        QSignalSpy failed(&saver, &DataSaver::recordingFailed);

        for (int cycle = 0; cycle < 2; cycle++) {
            saver.startRecording({});
            QVERIFY2(saver.isRecording(), qPrintable(QStringLiteral("cycle %1").arg(cycle)));
            saver.takeNote(QStringLiteral("note in cycle %1").arg(cycle));
            saver.stopRecording();
            QVERIFY(!saver.isRecording());
        }
        QCOMPARE(failed.count(), 0);

        // The second cycle re-truncated notes.csv: header + its one note.
        QFile notes(dir.path() + "/notes.csv");
        QVERIFY(notes.open(QFile::ReadOnly | QFile::Text));
        const QString contents = QString::fromUtf8(notes.readAll());
        QVERIFY(contents.contains("note in cycle 1"));
        QVERIFY(!contents.contains("note in cycle 0"));
    }

private:
    static QJsonObject deviceFreeConfig(const QString &dataDir)
    {
        QJsonObject config;
        config["dataDirectory"] = dataDir;
        config["directoryStructure"] = QJsonArray();
        return config;
    }

    static QString baseDirFor(const QStringList &structure)
    {
        QJsonObject config;
        config["dataDirectory"] = "/data";
        config["researcherName"] = "Test Person";
        QJsonArray tokens;
        for (const QString &entry : structure)
            tokens.append(entry);
        config["directoryStructure"] = tokens;

        DataSaver saver;
        saver.setUserConfig(config);
        saver.setRecordStartDateTime(
            QDateTime(QDate(2026, 7, 23), QTime(13, 4, 5)));
        saver.setupBaseDirectory();
        return saver.getBaseDirectory();
    }
};

QTEST_GUILESS_MAIN(TestDataSaver)
#include "tst_datasaver.moc"
