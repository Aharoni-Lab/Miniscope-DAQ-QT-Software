#include "bundlepaths.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace BundlePaths {

QString bundleResourcesDir(const QString &appDir)
{
    // <Bundle>.app/Contents/MacOS -> <Bundle>.app/Contents/Resources
    const QDir resources(appDir + "/../Resources");
    if (!resources.exists(QStringLiteral("deviceConfigs")))
        return QString();
    return resources.canonicalPath();
}

// QDir has no recursive copy; this is the minimal one (the data dirs are a
// handful of small JSON/script files, no symlinks).
static bool copyRecursively(const QString &src, const QString &dst)
{
    QDir srcDir(src);
    if (!srcDir.exists())
        return false;
    if (!QDir().mkpath(dst))
        return false;
    const auto entries =
        srcDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString target = dst + "/" + entry.fileName();
        if (entry.isDir()) {
            if (!copyRecursively(entry.absoluteFilePath(), target))
                return false;
        } else if (!QFile::copy(entry.absoluteFilePath(), target)) {
            return false;
        }
    }
    return true;
}

bool refreshWorkingData(const QString &srcRoot, const QString &workRoot,
                        const QStringList &dirs)
{
    bool ok = true;
    for (const QString &dir : dirs) {
        QDir old(workRoot + "/" + dir);
        if (old.exists())
            old.removeRecursively();
        if (!copyRecursively(srcRoot + "/" + dir, workRoot + "/" + dir)) {
            qWarning() << "BundlePaths: failed to copy" << dir << "from" << srcRoot
                       << "to" << workRoot;
            ok = false;
        }
    }
    return ok;
}

int seedDirectory(const QString &srcDir, const QString &dstDir)
{
    if (!QDir().mkpath(dstDir))
        return -1;
    int seeded = 0;
    const auto files = QDir(srcDir).entryInfoList(QDir::Files);
    for (const QFileInfo &file : files) {
        const QString target = dstDir + "/" + file.fileName();
        if (!QFile::exists(target) && QFile::copy(file.absoluteFilePath(), target))
            seeded++;
    }
    return seeded;
}

// Default an environment variable to dirPath (creating the directory) unless
// the user already set it; returns the effective directory.
static QString defaultEnvDir(const char *name, const QString &dirPath)
{
    QString dir = qEnvironmentVariable(name);
    if (dir.isEmpty()) {
        dir = dirPath;
        qputenv(name, dir.toUtf8());
    }
    QDir().mkpath(dir);
    return dir;
}

bool prepareBundleRuntime()
{
    const QString resources =
        bundleResourcesDir(QCoreApplication::applicationDirPath());
    if (resources.isEmpty())
        return false;   // dev build: keep the run-from-repo-root behavior

    // App-internal data lives in the app's writable data dir and tracks the
    // installed bundle; the bundle itself may be read-only (/Applications).
    const QString work =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    refreshWorkingData(resources, work, {QStringLiteral("deviceConfigs"),
                                         QStringLiteral("Scripts")});
    if (!QDir::setCurrent(work)) {
        qWarning() << "BundlePaths: could not enter working directory" << work;
        return false;
    }

    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + QStringLiteral("/Miniscope");
    const QString userConfigDir =
        defaultEnvDir("MINISCOPE_USERCONFIG_DIR", docs + QStringLiteral("/userConfigs"));
    seedDirectory(resources + QStringLiteral("/userConfigs"), userConfigDir);
    defaultEnvDir("MINISCOPE_DATA_DIR", docs + QStringLiteral("/data"));

    qInfo().nospace() << "Bundle runtime: working dir " << work
                      << ", user configs " << userConfigDir;
    return true;
}

} // namespace BundlePaths
