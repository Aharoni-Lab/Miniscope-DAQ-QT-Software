// Form-editor round-trip test: the ui-v3 config editor mutates m_userConfig
// directly through the backend's form API (setConfigValue / removeConfigKey /
// removeDevice / applyRawConfigJson), and saving must write exactly that
// object — including COMMENT_* annotations and unknown keys the form doesn't
// render, which the old JSON-tree editor silently dropped.
//
// Also instantiates the real ConfigForm.qml against the loaded backend to pin
// that the form builds its device rows from the config and that a QML-side
// edit lands in the config object.

#include <QtTest>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "backend.h"
#include "configvalidator.h"
#include "themecontroller.h"

class TestConfigForm : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void formApiRoundTrip();
    void newConfigValidatesClean();
    void dirtyTracking();
    void configFormQml();
    void directoryStructureArrayEdit();

private:
    // setUserConfigFileName() expects a file URL (it comes from QML file
    // dialogs / drops) and loads the config itself.
    static void loadExampleConfig(backEnd &backend)
    {
        const QString path =
            QDir::current().absoluteFilePath("userConfigs/UserConfigExample-primary.json");
        backend.setUserConfigFileName(QUrl::fromLocalFile(path).toString());
    }

    QTemporaryDir m_tempDir;
};

void TestConfigForm::initTestCase()
{
    QVERIFY2(QFile::exists("deviceConfigs/videoDevices.json"),
             "test must run with the repo root as its working directory");
    QVERIFY(m_tempDir.isValid());
}

void TestConfigForm::formApiRoundTrip()
{
    backEnd backend;
    loadExampleConfig(backend);

    QVariantMap cfg = backend.userConfigJson();
    QVERIFY(cfg.contains("devices"));

    // The example config carries COMMENT_* keys the form never renders.
    QStringList commentKeys;
    for (auto it = cfg.constBegin(); it != cfg.constEnd(); ++it)
        if (it.key().startsWith("COMMENT"))
            commentKeys << it.key();
    QVERIFY2(!commentKeys.isEmpty(),
             "expected COMMENT_* keys in the primary example config");

    // Form edits: top-level, nested device value, and an added custom key
    // (like a lab's own annotation) written through the raw-JSON path.
    QSignalSpy jsonChanged(&backend, &backEnd::userConfigJsonChanged);
    backend.setConfigValue({"researcherName"}, "Dr. Cortex");
    backend.setConfigValue({"recordLengthInSeconds"}, 42);
    QVERIFY(jsonChanged.count() >= 2);

    const QVariantMap devices = backend.userConfigJson().value("devices").toMap();
    const QVariantMap scopes = devices.value("miniscopes").toMap();
    QVERIFY(!scopes.isEmpty());
    const QString scopeName = scopes.firstKey();
    backend.setConfigValue({"devices", "miniscopes", scopeName, "framesPerFile"}, 500);

    // Integral doubles (every QML number) must store as JSON ints.
    const QJsonDocument probe = QJsonDocument::fromJson(backend.rawConfigJson().toUtf8());
    QCOMPARE(probe.object().value("recordLengthInSeconds"),
             QJsonValue(42)); // not 42.0

    // Save through the form path (no tree model involved) and re-read the file.
    const QString savedPath = m_tempDir.filePath("roundtrip.json");
    backend.saveConfigObjectAs(savedPath);

    QFile saved(savedPath);
    QVERIFY(saved.open(QFile::ReadOnly));
    const QJsonObject out = QJsonDocument::fromJson(saved.readAll()).object();

    QCOMPARE(out.value("researcherName").toString(), QStringLiteral("Dr. Cortex"));
    QCOMPARE(out.value("recordLengthInSeconds").toInt(), 42);
    QCOMPARE(out.value("devices").toObject()
                 .value("miniscopes").toObject()
                 .value(scopeName).toObject()
                 .value("framesPerFile").toInt(), 500);
    for (const QString &key : commentKeys)
        QVERIFY2(out.contains(key), qPrintable("dropped on save: " + key));
    // parseUserConfig() used to read via QJsonObject::operator[], which
    // INSERTS null keys it merely looked up — they must not reach the file.
    QVERIFY(!out.contains("dataStructureOrder"));
    QVERIFY(!out.contains("experiment"));

    // removeConfigKey / removeDevice actually remove.
    backend.removeConfigKey({"devices", "miniscopes", scopeName, "framesPerFile"});
    QVERIFY(!backend.userConfigJson().value("devices").toMap()
                 .value("miniscopes").toMap()
                 .value(scopeName).toMap().contains("framesPerFile"));
    backend.removeDevice("miniscopes", scopeName);
    QVERIFY(!backend.userConfigJson().value("devices").toMap()
                 .value("miniscopes").toMap().contains(scopeName));

    // Raw-JSON tab: a parse error changes nothing; valid text applies.
    const QString before = backend.rawConfigJson();
    QVERIFY(!backend.applyRawConfigJson("{ not json").isEmpty());
    QCOMPARE(backend.rawConfigJson(), before);
    QVERIFY(backend.applyRawConfigJson("{\"researcherName\": \"raw\"}").isEmpty());
    QCOMPARE(backend.userConfigJson().value("researcherName").toString(),
             QStringLiteral("raw"));
}

// A config built by New (and then filled in with devices) must satisfy the schema
// the app ships with. The generator derives its skeleton from the *types* in
// userConfigProps.json, which know nothing about the schema's enums, minimums and
// array lengths - so a bare skeleton carried configVersion 0, poseOverlay.type "",
// commutator.sampleRate 0 with empty axis arrays, and a string led0FineSteps, and
// the user's first edit dumped ~25 warning lines into the Config check panel.
void TestConfigForm::newConfigValidatesClean()
{
    backEnd backend;
    backend.newUserConfig();
    QVERIFY2(backend.configCheckNotes().isEmpty(),
             qPrintable("a brand-new config warns:\n  "
                        + backend.configCheckNotes().replace("\n", "\n  ")));

    // Every supported device type, since each one's defaults come from a
    // different catalog entry merged into the same schema template.
    for (const QString &category : {QStringLiteral("miniscopes"), QStringLiteral("cameras")}) {
        const QStringList types = backend.deviceTypesForCategory(category);
        QVERIFY2(!types.isEmpty(), qPrintable("no device types for " + category));
        for (const QString &type : types)
            backend.addDevice(category, type, type + QStringLiteral(" test"), 0);
    }
    QVERIFY2(backend.configCheckNotes().isEmpty(),
             qPrintable("a new config with devices warns:\n  "
                        + backend.configCheckNotes().replace("\n", "\n  ")));

    // Check the generated object against the schema directly as well, so this
    // stays a test of the defaults rather than of when the notes get refreshed.
    QJsonObject generated =
        QJsonDocument::fromJson(backend.rawConfigJson().toUtf8()).object();
    const QStringList warnings = checkUserConfig(generated);
    QVERIFY2(warnings.isEmpty(), qPrintable("generated config violates the schema:\n  "
                                            + warnings.join("\n  ")));

    // Stamped as written by this editor version (schema enum: [1]).
    QCOMPARE(generated.value("configVersion").toInt(), 1);
}

void TestConfigForm::dirtyTracking()
{
    backEnd backend;
    QSignalSpy dirtyChanged(&backend, &backEnd::configDirtyChanged);

    // Fresh load -> clean; any edit -> dirty.
    loadExampleConfig(backend);
    QVERIFY(!backend.configDirty());
    const QString original =
        backend.userConfigJson().value("animalName").toString();
    backend.setConfigValue({"animalName"}, original + "_edited");
    QVERIFY(backend.configDirty());
    QCOMPARE(dirtyChanged.count(), 1);

    // Editing back to the on-disk value -> clean again (dirty is a content
    // comparison, not an edit counter).
    backend.setConfigValue({"animalName"}, original);
    QVERIFY(!backend.configDirty());

    // Save As -> clean and the saved file's path is adopted.
    backend.setConfigValue({"animalName"}, "edited");
    QVERIFY(backend.configDirty());
    const QString pathBefore = backend.userConfigFileName();
    const QString savedPath = m_tempDir.filePath("dirty.json");
    backend.saveConfigObjectAs(savedPath);
    QVERIFY(!backend.configDirty());
    QCOMPARE(backend.userConfigFileName(), savedPath); // Save As adopts the path
    QVERIFY(pathBefore != savedPath);

    // New config -> dirty (exists nowhere on disk) with no file path.
    backend.newUserConfig();
    QVERIFY(backend.configDirty());
    QVERIFY(backend.userConfigFileName().isEmpty());

    // Saving the new config makes it clean and gives it its path.
    const QString newPath = m_tempDir.filePath("new.json");
    backend.saveConfigObjectAs(newPath);
    QVERIFY(!backend.configDirty());
    QCOMPARE(backend.userConfigFileName(), newPath);
}

void TestConfigForm::configFormQml()
{
    backEnd backend;
    loadExampleConfig(backend);

    QQmlEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    QQmlComponent component(&engine, QUrl("qrc:/ConfigForm.qml"));
    QScopedPointer<QObject> form(component.create());
    if (!form) {
        for (const auto &e : component.errors())
            qCritical() << e.toString();
        QFAIL("ConfigForm.qml failed to create");
    }

    // The device Repeater's model must reflect the config's devices.
    const QVariantList rows = form->property("deviceRows").toList();
    int expected = 0;
    const QVariantMap devs = backend.userConfigJson().value("devices").toMap();
    expected += devs.value("miniscopes").toMap().size();
    expected += devs.value("cameras").toMap().size();
    QVERIFY(expected > 0);
    QCOMPARE(rows.size(), expected);

    // A QML-side edit (what every field's write handler does) lands in the
    // config, and the form's live view reflects it.
    QVariant ok;
    QMetaObject::invokeMethod(form.data(), "set", Q_RETURN_ARG(QVariant, ok),
                              Q_ARG(QVariant, QVariantList{"animalName"}),
                              Q_ARG(QVariant, "mouse42"));
    QCOMPARE(backend.userConfigJson().value("animalName").toString(),
             QStringLiteral("mouse42"));

    // Removing a device from the backend shrinks the form's row model.
    const QString firstScope = devs.value("miniscopes").toMap().firstKey();
    backend.removeDevice("miniscopes", firstScope);
    QCOMPARE(form->property("deviceRows").toList().size(), expected - 1);

    // The trace-display card warns when the config has traceDisplay enabled
    // but no source that ever feeds traces. The example config enables
    // traceDisplay with its tracker disabled, so removing the only miniscope
    // (above) must surface the hint.
    QQuickItem *hint = form->findChild<QQuickItem *>("traceSourceHint");
    QVERIFY2(hint, "traceSourceHint missing from ConfigForm.qml");
    QVERIFY(hint->isVisible());
}

// The "Folder structure" field writes a JS array of strings through
// form.set -> backend.setConfigValue. A JS array arrives at a QVariant
// parameter wrapped in a QJSValue, which QJsonValue::fromVariant cannot turn
// into a JSON array on its own — so the value was stored as null, the config
// failed schema validation ("type not permitted"), and the field blanked on
// the next edit. This drives the real QML write path to pin the fix.
void TestConfigForm::directoryStructureArrayEdit()
{
    backEnd backend;
    loadExampleConfig(backend);

    QQmlEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    QQmlComponent component(&engine, QUrl("qrc:/ConfigForm.qml"));
    QScopedPointer<QObject> form(component.create());
    QVERIFY2(form, "ConfigForm.qml failed to create");

    // Exactly what the field's onTextEdited does: a JS array literal handed to
    // form.set(). QQmlExpression evaluates it in the form's own JS scope.
    QQmlExpression expr(engine.rootContext(), form.data(),
        "set(['directoryStructure'], ['researcherName', 'animalName', 'date', 'time'])");
    bool valueIsUndefined = false;
    expr.evaluate(&valueIsUndefined);
    QVERIFY2(!expr.hasError(), qPrintable(expr.error().toString()));

    // Must be stored as a 4-element string array, not null.
    const QVariantList stored =
        backend.userConfigJson().value("directoryStructure").toList();
    QCOMPARE(stored.size(), 4);
    QCOMPARE(stored.at(0).toString(), QStringLiteral("researcherName"));
    QCOMPARE(stored.at(3).toString(), QStringLiteral("time"));

    // The raw JSON must round-trip as a JSON array of strings...
    const QJsonValue ds = QJsonDocument::fromJson(backend.rawConfigJson().toUtf8())
                              .object().value("directoryStructure");
    QVERIFY2(ds.isArray(), "directoryStructure did not serialize as a JSON array");
    QCOMPARE(ds.toArray().size(), 4);

    // ...and the schema check must not reject it.
    QVERIFY2(!backend.configCheckNotes().contains("directoryStructure"),
             qPrintable("config check flagged directoryStructure: "
                        + backend.configCheckNotes()));
}

int main(int argc, char *argv[])
{
    QQuickStyle::setStyle("Basic");  // match the app; allows control customization
    QGuiApplication app(argc, argv); // QML needs a Gui app
    registerMiniscopeQmlTypes();     // ThemeState / Theme singletons
    TestConfigForm tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_configform.moc"
