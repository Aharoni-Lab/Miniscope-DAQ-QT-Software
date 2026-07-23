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
#include <QJsonArray>
#include <QJsonObject>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>

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
        QCOMPARE(lines[0], QStringLiteral("Frame Number,Time Stamp (ms),Buffer Index,DAQ Frame Number"));
        for (int i = 0; i < 3; i++) {
            const QStringList cols = lines[i + 1].split(',');
            QCOMPARE(cols.size(), 4);
            QCOMPARE(cols[0].toInt(), i);
            QCOMPARE(cols[3].toLongLong(), daqValues[i]);
        }

        // The drained frames must be in the video file, too.
        cv::VideoCapture readBack((dir.path() + "/Scope/0.avi").toStdString());
        QVERIFY(readBack.isOpened());
        QCOMPARE(int(readBack.get(cv::CAP_PROP_FRAME_COUNT)), 3);
    }

    void devicesWithoutDaqCounterKeepThreeColumnCsv()
    {
        // Behavior webcams have no DAQ counter (nullptr buffer): their CSV
        // format must stay exactly as it always was.
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
        QCOMPARE(header, QStringLiteral("Frame Number,Time Stamp (ms),Buffer Index"));
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
