#ifndef BUNDLEPATHS_H
#define BUNDLEPATHS_H

#include <QString>
#include <QStringList>

// Runtime path setup for the packaged macOS .app bundle.
//
// The app reads its runtime data (./deviceConfigs, ./Scripts) and the example
// user configs relative to the WORKING DIRECTORY - fine for a dev build run
// from the repo root, wrong for a double-clicked .app whose working directory
// is "/". The Windows launcher and the Linux AppImage wrapper solve this
// outside the app; on macOS a wrapper script inside the bundle would break
// code signing and TCC camera-permission attribution, so the same setup runs
// natively in main() instead (prepareBundleRuntime below, macOS-only call).
//
// The helpers are plain path->path functions so the unit tests can exercise
// them against temp directories on every platform.
namespace BundlePaths {

// Resources directory of the bundle containing the app binary, or an empty
// string when the binary does not live in a bundle with packaged runtime data
// (e.g. a plain dev build) - detected by <appDir>/../Resources/deviceConfigs
// existing. appDir is QCoreApplication::applicationDirPath().
QString bundleResourcesDir(const QString &appDir);

// Refresh-copy each named subdirectory of srcRoot into workRoot (delete the
// old copy, then copy - so app-internal data always matches the installed
// version). Returns false if any copy fails.
bool refreshWorkingData(const QString &srcRoot, const QString &workRoot,
                        const QStringList &dirs);

// Copy the files of srcDir into dstDir, never overwriting existing files
// (user edits survive upgrades). Creates dstDir. Returns the number seeded,
// or -1 if dstDir cannot be created.
int seedDirectory(const QString &srcDir, const QString &dstDir);

// Full bundle bootstrap, called from main() on macOS before the backend is
// constructed (it reads ./deviceConfigs in its constructor). No-op (returns
// false) for non-bundled dev builds. When bundled:
//   * refreshes deviceConfigs + Scripts into the app's writable data dir
//     (~/Library/Application Support/MiniscopeDAQ) and makes it the working
//     directory,
//   * defaults MINISCOPE_USERCONFIG_DIR / MINISCOPE_DATA_DIR (respecting
//     preexisting values) to ~/Documents/Miniscope/{userConfigs,data} and
//     seeds the example user configs there - the same contract the Linux
//     AppImage wrapper establishes.
bool prepareBundleRuntime();

} // namespace BundlePaths

#endif // BUNDLEPATHS_H
