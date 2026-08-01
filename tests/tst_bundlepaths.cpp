#include <QtTest>
#include <QTemporaryDir>

#include "bundlepaths.h"

// The .app bundle's runtime path setup (macOS packaging). Wrong behavior here
// means a packaged app silently loses device configs on upgrade or clobbers a
// user's edited configs - so the copy semantics are pinned down by test:
//   * refreshWorkingData REPLACES (app-internal data tracks the bundle),
//   * seedDirectory NEVER overwrites (user edits survive upgrades).
class TestBundlePaths : public QObject
{
    Q_OBJECT

private slots:
    void detectsBundleLayout();
    void rejectsNonBundleLayout();
    void refreshReplacesStaleData();
    void refreshReportsMissingSource();
    void seedNeverOverwrites();
    void seedCreatesDestination();
};

// Build <root>/Contents/{MacOS,Resources/deviceConfigs} like a real bundle.
static QString makeBundle(const QTemporaryDir &root)
{
    const QString contents = root.path() + "/Fake.app/Contents";
    QDir().mkpath(contents + "/MacOS");
    QDir().mkpath(contents + "/Resources/deviceConfigs");
    return contents;
}

static void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

void TestBundlePaths::detectsBundleLayout()
{
    QTemporaryDir root;
    const QString contents = makeBundle(root);
    const QString resources = BundlePaths::bundleResourcesDir(contents + "/MacOS");
    QVERIFY(!resources.isEmpty());
    QCOMPARE(QFileInfo(resources).fileName(), QStringLiteral("Resources"));
}

void TestBundlePaths::rejectsNonBundleLayout()
{
    // A dev build directory (no ../Resources/deviceConfigs) must return empty
    // so main() keeps the run-from-repo-root behavior.
    QTemporaryDir root;
    QVERIFY(BundlePaths::bundleResourcesDir(root.path()).isEmpty());
}

void TestBundlePaths::refreshReplacesStaleData()
{
    QTemporaryDir src, work;
    writeFile(src.path() + "/deviceConfigs/videoDevices.json", "new");
    writeFile(src.path() + "/deviceConfigs/sub/nested.json", "nested");
    // Stale copy from a previous app version, including a removed file.
    writeFile(work.path() + "/deviceConfigs/videoDevices.json", "old");
    writeFile(work.path() + "/deviceConfigs/removedInNewVersion.json", "stale");

    QVERIFY(BundlePaths::refreshWorkingData(src.path(), work.path(),
                                            {QStringLiteral("deviceConfigs")}));
    QCOMPARE(readFile(work.path() + "/deviceConfigs/videoDevices.json"),
             QByteArray("new"));
    QCOMPARE(readFile(work.path() + "/deviceConfigs/sub/nested.json"),
             QByteArray("nested"));
    QVERIFY(!QFile::exists(work.path() + "/deviceConfigs/removedInNewVersion.json"));
}

void TestBundlePaths::refreshReportsMissingSource()
{
    QTemporaryDir src, work;
    QVERIFY(!BundlePaths::refreshWorkingData(src.path(), work.path(),
                                             {QStringLiteral("doesNotExist")}));
}

void TestBundlePaths::seedNeverOverwrites()
{
    QTemporaryDir src, dst;
    writeFile(src.path() + "/UserConfigExample.json", "shipped");
    writeFile(src.path() + "/another.json", "shipped2");
    // The user edited this one; an upgrade must not clobber it.
    writeFile(dst.path() + "/UserConfigExample.json", "user-edited");

    QCOMPARE(BundlePaths::seedDirectory(src.path(), dst.path()), 1);
    QCOMPARE(readFile(dst.path() + "/UserConfigExample.json"),
             QByteArray("user-edited"));
    QCOMPARE(readFile(dst.path() + "/another.json"), QByteArray("shipped2"));

    // Second run: everything already present, nothing to seed.
    QCOMPARE(BundlePaths::seedDirectory(src.path(), dst.path()), 0);
}

void TestBundlePaths::seedCreatesDestination()
{
    QTemporaryDir src, root;
    writeFile(src.path() + "/a.json", "a");
    const QString dst = root.path() + "/deep/new/dir";
    QCOMPARE(BundlePaths::seedDirectory(src.path(), dst), 1);
    QCOMPARE(readFile(dst + "/a.json"), QByteArray("a"));
}

QTEST_MAIN(TestBundlePaths)
#include "tst_bundlepaths.moc"
