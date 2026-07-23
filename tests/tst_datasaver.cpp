// Regression tests for DataSaver configuration handling:
//  - setHeadOrientationConfig once assigned the filter flag over the enable
//    flag (copy-paste bug), silently suppressing the headOrientation.csv
//    whenever filterBadData was false.
//  - The reserved "date"/"time" directoryStructure tokens were matched
//    case-sensitively, so a capitalized config entry ("Date") produced a
//    literal "DateMissing" folder instead of the date.

#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonObject>

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

private:
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
