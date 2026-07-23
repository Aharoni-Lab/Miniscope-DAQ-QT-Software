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
        saver.setFrameBufferParameters("Cam", frames, timestamps, nullptr, 1,
                                       &freeFrames, &usedFrames, &acqFrame);

        QSignalSpy failed(&saver, &DataSaver::recordingFailed);
        saver.startRecording({});
        QVERIFY(!saver.isRecording());
        QCOMPARE(failed.count(), 1);
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
