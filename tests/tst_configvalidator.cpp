#include <QtTest>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "configvalidator.h"

// User-config schema validation and key migration.
//
// Policy under test (lab decision, July 2026): validation warns, never
// blocks; extra keys are always allowed; deprecated spellings keep working
// forever via in-memory migration.
class TestConfigValidator : public QObject
{
    Q_OBJECT

private slots:
    void migrateRenamesOldRecordLengthKey();
    void migratePrefersCanonicalWhenBothPresent();
    void migrateLeavesCanonicalConfigsUntouched();
    void schemaFileParses();
    void allShippedExampleConfigsValidate();
    void wrongTypeIsReportedWithPath();
    void extraKeysAreAllowed();
    void missingRequiredKeyWarns();
    void legacyDeviceArrayFormValidates();
    void missingSchemaFileWarnsInsteadOfBlocking();
    void catalogFineStepsBlocksAreWellFormed();

private:
    QJsonObject loadJson(const QString &path);
    QJsonObject loadSchema();
};

QJsonObject TestConfigValidator::loadJson(const QString &path)
{
    QFile f(path);
    const bool opened = f.open(QIODevice::ReadOnly | QIODevice::Text);
    if (!opened)
        return QJsonObject();
    QJsonParseError err;
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError)
        return QJsonObject();
    return d.object();
}

QJsonObject TestConfigValidator::loadSchema()
{
    return loadJson(QStringLiteral(REPO_SOURCE_DIR "/deviceConfigs/userConfigSchema.json"));
}

void TestConfigValidator::migrateRenamesOldRecordLengthKey()
{
    QJsonObject config{{"recordLengthinSeconds", 600}};
    const QStringList notes = migrateUserConfig(config);
    QVERIFY(!config.contains("recordLengthinSeconds"));
    QCOMPARE(config["recordLengthInSeconds"].toInt(), 600);
    QCOMPARE(notes.size(), 1);
    QVERIFY(notes[0].contains("deprecated"));
}

void TestConfigValidator::migratePrefersCanonicalWhenBothPresent()
{
    QJsonObject config{{"recordLengthinSeconds", 600},
                       {"recordLengthInSeconds", 30}};
    migrateUserConfig(config);
    QCOMPARE(config["recordLengthInSeconds"].toInt(), 30);
    QVERIFY(!config.contains("recordLengthinSeconds"));
}

void TestConfigValidator::migrateLeavesCanonicalConfigsUntouched()
{
    QJsonObject config{{"recordLengthInSeconds", 30}, {"dataDirectory", "/tmp"}};
    const QJsonObject before = config;
    QVERIFY(migrateUserConfig(config).isEmpty());
    QCOMPARE(config, before);
}

void TestConfigValidator::schemaFileParses()
{
    const QJsonObject schema = loadSchema();
    QVERIFY(!schema.isEmpty());
    // Feeding an empty config through exercises valijson's schema parse; a
    // malformed schema comes back as its own warning, which must not happen.
    const QStringList warnings = validateUserConfigAgainstSchema(QJsonObject(), schema);
    for (const QString &w : warnings)
        QVERIFY2(!w.contains("schema could not be parsed"), qPrintable(w));
}

void TestConfigValidator::allShippedExampleConfigsValidate()
{
    const QJsonObject schema = loadSchema();
    QVERIFY(!schema.isEmpty());

    int checked = 0;
    QDirIterator it(QStringLiteral(REPO_SOURCE_DIR "/userConfigs"), {"*.json"},
                    QDir::Files);
    while (it.hasNext()) {
        const QString path = it.next();
        QJsonObject config = loadJson(path);
        QVERIFY2(!config.isEmpty(), qPrintable(path + " did not parse"));
        migrateUserConfig(config);
        const QStringList warnings = validateUserConfigAgainstSchema(config, schema);
        QVERIFY2(warnings.isEmpty(),
                 qPrintable(path + ":\n  " + warnings.join("\n  ")));
        checked++;
    }
    QVERIFY(checked >= 8);   // the six examples plus the two Linux test configs
}

void TestConfigValidator::wrongTypeIsReportedWithPath()
{
    QJsonObject config{
        {"dataDirectory", "/tmp"},
        {"devices", QJsonObject{
            {"cameras", QJsonObject{
                {"My Cam", QJsonObject{{"deviceType", "WebCam"},
                                       {"deviceID", "zero"}}}   // wrong type
            }}
        }}
    };
    const QStringList warnings = validateUserConfigAgainstSchema(config, loadSchema());
    QVERIFY(!warnings.isEmpty());
    bool found = false;
    for (const QString &w : warnings)
        if (w.contains("deviceID") && w.contains("My Cam"))
            found = true;
    QVERIFY2(found, qPrintable(warnings.join("\n")));
}

void TestConfigValidator::extraKeysAreAllowed()
{
    // Labs keep notes, COMMENT_* keys, and custom directoryStructure tokens
    // in their configs; none of that may warn.
    QJsonObject config{
        {"dataDirectory", "/tmp"},
        {"devices", QJsonObject{}},
        {"COMMENT_recordLength", "set to 0 for manual stop"},
        {"myLabsCustomToken", "sessionA"},
        {"notes", "rig 2, chronic implant"}
    };
    QCOMPARE(validateUserConfigAgainstSchema(config, loadSchema()), QStringList());
}

void TestConfigValidator::missingRequiredKeyWarns()
{
    // dataDirectory and devices are the two keys the software cannot run
    // without; their absence is worth a warning (but still never blocks).
    const QStringList warnings =
        validateUserConfigAgainstSchema(QJsonObject{{"researcherName", "x"}}, loadSchema());
    QVERIFY(!warnings.isEmpty());
}

void TestConfigValidator::legacyDeviceArrayFormValidates()
{
    // Pre-2.0 configs carry devices.cameras as an ARRAY with per-element
    // deviceName; those files are still accepted (loadUserConfigFile converts
    // them), so the schema must accept them too.
    QJsonObject config{
        {"dataDirectory", "/tmp"},
        {"devices", QJsonObject{
            {"cameras", QJsonArray{
                QJsonObject{{"deviceName", "BehavCam"},
                            {"deviceType", "WebCam"},
                            {"deviceID", 0}}
            }}
        }}
    };
    QCOMPARE(validateUserConfigAgainstSchema(config, loadSchema()), QStringList());
}

void TestConfigValidator::missingSchemaFileWarnsInsteadOfBlocking()
{
    QJsonObject config{{"dataDirectory", "/tmp"}};
    const QStringList messages =
        checkUserConfig(config, QStringLiteral("/nonexistent/schema.json"));
    QCOMPARE(messages.size(), 1);
    QVERIFY(messages[0].contains("skipping config validation"));
}

// The fine-steps feature's fragile surface is the shipped catalog, not the
// 5-line merge in videodevice.cpp: a videoDevices.json edit could drop a V4
// "fineSteps" block, typo a key (merged keys flow into QQuickItem::setProperty,
// which fails SILENTLY for unknown properties), or override semantic fields
// like sendCommand. Pin the shape here so catalog edits can't rot the flag.
void TestConfigValidator::catalogFineStepsBlocksAreWellFormed()
{
    const QJsonObject catalog =
        loadJson(QStringLiteral(REPO_SOURCE_DIR "/deviceConfigs/videoDevices.json"));
    QVERIFY(!catalog.isEmpty());

    int fineStepsBlocks = 0;
    for (auto dev = catalog.constBegin(); dev != catalog.constEnd(); ++dev) {
        const QJsonObject controls = dev.value().toObject()["controlSettings"].toObject();
        for (auto ctl = controls.constBegin(); ctl != controls.constEnd(); ++ctl) {
            const QJsonObject control = ctl.value().toObject();
            if (!control.contains("fineSteps"))
                continue;
            fineStepsBlocks++;
            const QJsonObject fineSteps = control["fineSteps"].toObject();
            QVERIFY2(!fineSteps.isEmpty(),
                     qPrintable(dev.key() + "/" + ctl.key() + ": fineSteps must be a non-empty object"));
            for (auto it = fineSteps.constBegin(); it != fineSteps.constEnd(); ++it) {
                const QString where = dev.key() + "/" + ctl.key() + "/fineSteps/" + it.key();
                // Only override keys the control itself defines - anything else
                // would reach setProperty() on the QML item and fail silently.
                QVERIFY2(control.contains(it.key()),
                         qPrintable(where + " overrides a key the control does not define"));
                // Overriding the wire command or the boot value is beyond what
                // the flag means (a finer slider mapping).
                QVERIFY2(it.key() != "sendCommand" && it.key() != "startValue",
                         qPrintable(where + " must not override command/startValue semantics"));
                QVERIFY2(it.value().type() == control[it.key()].type(),
                         qPrintable(where + " changes the JSON type of the key it overrides"));
            }
        }
    }

    // The shipped feature: every LED slider in the catalog carries the 0-255
    // mapping (one hardware step per tick on the inverted register) - led0 on
    // the single-LED V4 variants, and both LEDs on the dual-color scopes,
    // whose second excitation LED runs the same driver through the other
    // wiper of the same digital pot.
    struct LedFineSteps { const char *device; const char *control; };
    static const LedFineSteps expected[] = {
        {"Miniscope_V4",           "led0"},
        {"Miniscope_V4_BNO",       "led0"},
        {"Miniscope_V4_2C_Manual", "led0"},
        {"Miniscope_V4_2C_Manual", "led1"},
    };
    for (const LedFineSteps &led : expected) {
        const QString where = QString::fromLatin1(led.device) + "/"
                              + QString::fromLatin1(led.control);
        const QJsonObject fineSteps =
            catalog[QString::fromLatin1(led.device)].toObject()["controlSettings"]
                .toObject()[QString::fromLatin1(led.control)].toObject()["fineSteps"].toObject();
        QVERIFY2(!fineSteps.isEmpty(), qPrintable(where + " lost its fineSteps block"));
        QCOMPARE(fineSteps["max"].toDouble(), 255.0);
        QCOMPARE(fineSteps["displayValueScale"].toDouble(), -1.0);
    }
    // Pinned against the list above rather than a literal, so adding an LED
    // slider means adding it here too - a catalog-only edit still trips this.
    QCOMPARE(fineStepsBlocks, int(sizeof(expected) / sizeof(expected[0])));
}

QTEST_MAIN(TestConfigValidator)
#include "tst_configvalidator.moc"
