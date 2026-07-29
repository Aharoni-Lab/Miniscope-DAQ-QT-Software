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
    void codecModelOrdering();
    void led0FineStepsHints();
    void duplicateDeviceNamesAreRefused();
    void runReportsProgressAndClearsIt();
    void enableSwitchSitsWithItsHeading();
    void directoryStructureArrayEdit();

private:
    // setUserConfigFileName() expects a file URL (it comes from QML file
    // dialogs / drops) and loads the config itself.
    static void loadExampleConfig(backEnd &backend)
    {
        // The annotated reference config: one V4_BNO Miniscope (fine-step
        // capable, led0 = 10) plus one camera, traceDisplay on, tracker off -
        // the combination the cases below assert against.
        const QString path =
            QDir::current().absoluteFilePath("userConfigs/Reference-AllOptions.json");
        backend.setUserConfigFileName(QUrl::fromLocalFile(path).toString());
    }

    // Items a Repeater created (every device row) are not QObject children of
    // the form root, so findChild() can't see into them - walk the visual tree.
    static QQuickItem *findVisualChild(QQuickItem *root, const QString &objectName)
    {
        if (!root)
            return nullptr;
        const QList<QQuickItem *> kids = root->childItems();
        for (QQuickItem *kid : kids) {
            if (kid->objectName() == objectName)
                return kid;
            if (QQuickItem *hit = findVisualChild(kid, objectName))
                return hit;
        }
        return nullptr;
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

// The codec dropdown must recommend by device class, not by whatever order the
// host's codec probe happened to produce: Miniscope imaging has to stay lossless
// (GREY / FFV1), while behavior video is a natural scene a lossy codec handles
// well (MJPG / XVID). Only the ORDER changes - every host-supported codec stays
// selectable, and a codec the config names but the host lacks stays visible.
void TestConfigForm::codecModelOrdering()
{
    backEnd backend;
    loadExampleConfig(backend);

    QQmlEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    QQmlComponent component(&engine, QUrl("qrc:/ConfigForm.qml"));
    QScopedPointer<QObject> form(component.create());
    QVERIFY2(form, "ConfigForm.qml failed to create");

    const auto model = [&](const QString &js) {
        QQmlExpression expr(engine.rootContext(), form.data(), js);
        const QVariant v = expr.evaluate();
        if (expr.hasError())
            qWarning() << expr.error().toString();
        return v.toStringList();
    };

    const QStringList host = backend.availableCodecs();
    QVERIFY2(!host.isEmpty(), "host reported no codecs at all");

    // Whichever of the recommended codecs this host actually has must come
    // first, in the recommended order.
    const auto assertLeads = [&](const QString &category, const QStringList &preferred) {
        const QStringList offered = model(QStringLiteral("codecModel('', '%1')").arg(category));
        QStringList lead;
        for (const QString &codec : preferred)
            if (host.contains(codec))
                lead.append(codec);
        // Nothing to assert about the lead on a host that has none of them
        // (QSKIP can't be used here - this is a lambda, not the test slot).
        if (lead.isEmpty())
            qWarning() << "host supports none of the recommended codecs for" << category;
        QCOMPARE(offered.mid(0, lead.size()), lead);

        // Same set, reordered - nothing dropped from the dropdown.
        QStringList sortedOffered = offered;
        QStringList sortedHost = host;
        sortedOffered.sort();
        sortedHost.sort();
        QCOMPARE(sortedOffered, sortedHost);
    };

    assertLeads(QStringLiteral("miniscopes"), {"GREY", "FFV1"});
    assertLeads(QStringLiteral("cameras"), {"MJPG", "XVID"});

    // A codec the config file names but this host can't provide is still shown
    // (and first, since it's the row's current value) rather than silently
    // reading as some other codec.
    const QStringList withUnsupported = model("codecModel('NOPE', 'miniscopes')");
    QCOMPARE(withUnsupported.size(), host.size() + 1);
    QCOMPARE(withUnsupported.first(), QStringLiteral("NOPE"));
}

// The led0 "Fine steps" switch had no explanation in the form - the range in
// its label was the only clue, and the userConfigProps tip for it is only ever
// surfaced on FormRow labels, which this control isn't. Both hint lines are
// derived from the catalog (not hardcoded), and the units caution only appears
// while the flag is on, so pin the derived numbers and that visibility.
void TestConfigForm::led0FineStepsHints()
{
    backEnd backend;
    loadExampleConfig(backend);

    QQmlEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    QQmlComponent component(&engine, QUrl("qrc:/ConfigForm.qml"));
    QScopedPointer<QObject> form(component.create());
    QVERIFY2(form, "ConfigForm.qml failed to create");

    // The example config's Miniscope_V4_BNO is a fine-step-capable type.
    QQuickItem *formItem = qobject_cast<QQuickItem *>(form.data());
    QVERIFY(formItem);
    QQuickItem *hint = findVisualChild(formItem, "led0FineStepsHint");
    QVERIFY2(hint, "led0FineStepsHint missing from ConfigForm.qml");
    QQuickItem *units = findVisualChild(formItem, "led0FineStepsUnitsHint");
    QVERIFY2(units, "led0FineStepsUnitsHint missing from ConfigForm.qml");

    // Both live in the device row's edit drawer, which starts collapsed.
    QQuickItem *row = hint;
    while (row && !row->property("editOpen").isValid())
        row = row->parentItem();
    QVERIFY2(row, "the hint is not inside a device row");
    row->setProperty("editOpen", true);

    QVERIFY(hint->isVisible());
    const QString what = hint->property("text").toString();
    QVERIFY2(what.contains("255"), qPrintable(what));  // fine range, from the catalog
    QVERIFY2(what.contains("2.55"), qPrintable(what)); // computed 255/100, not hardcoded

    // The units caution is only relevant once the flag is on: the example
    // config leaves it off.
    QVERIFY(!units->isVisible());

    const QString scope =
        backend.userConfigJson().value("devices").toMap()
            .value("miniscopes").toMap().firstKey();
    backend.setConfigValue({"devices", "miniscopes", scope, "led0FineSteps"}, true);
    QVERIFY(units->isVisible());

    // The config's led0 is 10, which on the 0-255 scale is 4% of full power -
    // not the 10% the same number meant before.
    const QString caution = units->property("text").toString();
    QVERIFY2(caution.contains(QStringLiteral("led0 = 10")), qPrintable(caution));
    QVERIFY2(caution.contains(QStringLiteral("4%")), qPrintable(caution));
}

// A device name is the folder that device records into, so a duplicate is not a
// cosmetic problem: two devices would write their video and timeStamps.csv into
// one directory. Add Device used to no-op silently on a name taken in the same
// category (so it looked like the Add just didn't happen) and accept one taken
// in the OTHER category outright.
void TestConfigForm::duplicateDeviceNamesAreRefused()
{
    backEnd backend;
    loadExampleConfig(backend);

    const QVariantMap devices = backend.userConfigJson().value("devices").toMap();
    const QString scope = devices.value("miniscopes").toMap().firstKey();
    const QString cam = devices.value("cameras").toMap().firstKey();
    QVERIFY(!scope.isEmpty() && !cam.isEmpty());

    const QStringList names = backend.configuredDeviceNames();
    QVERIFY2(names.contains(scope) && names.contains(cam), qPrintable(names.join(", ")));

    // Same category, same name.
    QVERIFY(!backend.deviceNameProblem(scope).isEmpty());
    QVERIFY(!backend.addDevice("miniscopes", "Miniscope_V4_BNO", scope, 3));
    // The other category is no better: it is the same folder either way.
    QVERIFY(!backend.deviceNameProblem(cam).isEmpty());
    QVERIFY(!backend.addDevice("miniscopes", "Miniscope_V4_BNO", cam, 3));

    // Names that differ only by case, or by space-vs-underscore, are the same
    // directory on Windows and macOS.
    QVERIFY(!backend.deviceNameProblem(scope.toUpper()).isEmpty());
    QVERIFY(!backend.deviceNameProblem(QString(cam).replace(' ', '_')).isEmpty());
    QVERIFY(!backend.addDevice("cameras", "WebCam", scope.toLower(), 4));
    QVERIFY(!backend.deviceNameProblem("   ").isEmpty());   // and blank is no name

    // The device count never moved through any of that.
    const QVariantMap after = backend.userConfigJson().value("devices").toMap();
    QCOMPARE(after.value("miniscopes").toMap().size(),
             devices.value("miniscopes").toMap().size());
    QCOMPARE(after.value("cameras").toMap().size(), devices.value("cameras").toMap().size());

    // A free name works, and the suggestion the dialog prefills is always free -
    // "<base> 2" once <base> is taken.
    const QString suggested = backend.uniqueDeviceName(scope);
    QVERIFY2(suggested != scope, qPrintable(suggested));
    QVERIFY(backend.deviceNameProblem(suggested).isEmpty());
    QVERIFY(backend.addDevice("miniscopes", "Miniscope_V4_BNO", suggested, 5));
    QVERIFY(backend.configuredDeviceNames().contains(suggested));
    // ...and it keeps counting once that one is taken too.
    QVERIFY(backend.uniqueDeviceName(scope) != suggested);

    // Whitespace around a name is trimmed, not stored (it would be invisible in
    // the form and produce a folder with a trailing space).
    QVERIFY(backend.addDevice("cameras", "WebCam", "  Spaced Cam  ", 6));
    QVERIFY(backend.configuredDeviceNames().contains("Spaced Cam"));

    // The config still satisfies its own schema after all that.
    QVERIFY2(backend.configCheckNotes().isEmpty(), qPrintable(backend.configCheckNotes()));
}

// Run blocks the GUI thread while it opens devices, so it publishes what it is
// doing (startupStage) and the shell shows an overlay while `starting` is true.
// A config that FAILS its checks is the path that matters here: it returns early,
// and if `starting` stayed true the Run button and the overlay would lock the UI
// out for good.
void TestConfigForm::runReportsProgressAndClearsIt()
{
    backEnd backend;
    loadExampleConfig(backend);

    // A codec this host doesn't have: checkUserConfigForIssues rejects the
    // config, and it does so BEFORE any device is opened - so this stays a unit
    // test and never touches a camera.
    const QString scope = backend.userConfigJson().value("devices").toMap()
                              .value("miniscopes").toMap().firstKey();
    QVERIFY(!backend.availableCodecs().contains("ZZZZ"));
    backend.setConfigValue({"devices", "miniscopes", scope, "compression"}, "ZZZZ");

    QStringList stages;
    connect(&backend, &backEnd::startupStageChanged, &backend, [&] {
        if (!backend.startupStage().isEmpty())
            stages << backend.startupStage();
    });

    backend.onRunClicked();
    QVERIFY2(!backend.sessionActive(), "a config with an unsupported codec must not run");
    QVERIFY2(!backend.starting(), "the starting flag was left set after a failed Run");
    QVERIFY(backend.startupStage().isEmpty());
    // It said something before giving up, rather than freezing silently.
    QVERIFY2(!stages.isEmpty(), "Run reported no progress at all");
}

// The "Folder structure" field writes a JS array of strings through
// form.set -> backend.setConfigValue. A JS array arrives at a QVariant
// parameter wrapped in a QJSValue, which QJsonValue::fromVariant cannot turn
// into a JSON array on its own — so the value was stored as null, the config
// failed schema validation ("type not permitted"), and the field blanked on
// the next edit. This drives the real QML write path to pin the fix.
// A feature card's enable switch used to sit at the far right edge with the
// heading at the far left, which reads as unrelated furniture: the reference
// config is the exact failure case - a commutator with its serial port filled in
// (COM3) and `enabled` still false. The switch now sits with the title, says its
// state in words, and an open-but-disabled card says so in its body.
void TestConfigForm::enableSwitchSitsWithItsHeading()
{
    backEnd backend;
    loadExampleConfig(backend);
    QCOMPARE(backend.userConfigJson().value("commutator").toMap()
                 .value("enabled").toBool(), false);

    QQmlEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    QQmlComponent component(&engine, QUrl("qrc:/ConfigForm.qml"));
    QScopedPointer<QObject> form(component.create());
    QVERIFY2(form, "ConfigForm.qml failed to create");
    auto *formItem = qobject_cast<QQuickItem *>(form.data());
    QVERIFY(formItem);
    formItem->setWidth(900);
    formItem->setHeight(1200);

    QQuickItem *card = findVisualChild(formItem, "commutatorCard");
    QVERIFY2(card, "commutatorCard missing from ConfigForm.qml");
    QQuickItem *sw = findVisualChild(card, "enableSwitch");
    QQuickItem *state = findVisualChild(card, "enableStateText");
    QQuickItem *hint = findVisualChild(card, "disabledHint");
    QQuickItem *title = findVisualChild(card, "cardTitle");
    QVERIFY(sw);
    QVERIFY(state);
    QVERIFY(hint);
    QVERIFY(title);

    // Directly after the title - both are siblings in the same header row now,
    // so the gap is one layout spacing. It used to be pushed past the subtitle's
    // full width to the card's right edge, which is what made it missable. This
    // is deliberately measured against the title rather than against the card
    // width: the assertion has to hold at any card width.
    const qreal gap = sw->x() - (title->x() + title->width());
    QVERIFY2(gap >= 0 && gap < 40,
             qPrintable(QStringLiteral("switch is %1 px from the title (switch x=%2, title ends %3)")
                            .arg(gap).arg(sw->x())
                            .arg(title->x() + title->width())));
    // ...and the state word reads with the switch, on its far side.
    QVERIFY2(state->x() > sw->x(), "the state word is not grouped with the switch");
    QCOMPARE(state->property("text").toString(), QStringLiteral("Disabled"));

    // The body hint appears once the card is open - the point at which someone
    // is filling in the port and about to forget the switch.
    card->setProperty("expanded", true);
    QVERIFY2(hint->isVisible(), "an open, disabled card does not say it is off");

    // Enabling from either affordance clears it and updates the word.
    backend.setConfigValue({"commutator", "enabled"}, true);
    QVERIFY2(!hint->isVisible(), "the hint survived enabling the feature");
    QCOMPARE(state->property("text").toString(), QStringLiteral("Enabled"));
}

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
