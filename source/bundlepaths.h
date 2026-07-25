#ifndef BUNDLEPATHS_H
#define BUNDLEPATHS_H

#include <QString>
#include <QStringList>

// Runtime path setup for packaged builds.
//
// The app reads its runtime data (./deviceConfigs, ./Scripts) and the example
// user configs relative to the WORKING DIRECTORY - fine for a dev build run
// from the repo root, wrong for a double-clicked .app whose working directory
// is "/". The Linux AppImage wrapper solves this entirely outside the app; on
// macOS a wrapper script inside the bundle would break code signing and TCC
// camera-permission attribution, so the full setup runs natively in main()
// instead (prepareBundleRuntime below, macOS-only call). On Windows the
// launcher already puts the working directory at the install root, so main()
// only needs the user-config/data half of the contract (seedUserDataDirs).
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

// Point the user's config/data folders at ~/Documents/Miniscope and seed the
// example user configs there (from seedRoot/userConfigs, never clobbering
// existing files). Defaults MINISCOPE_USERCONFIG_DIR / MINISCOPE_DATA_DIR
// (respecting any preexisting values) so the config open/save dialogs and the
// new-config data directory land in ~/Documents/Miniscope/{userConfigs,data} -
// OUTSIDE the install dir, so the user's own configs and recordings survive an
// upgrade (which refreshes the install dir) and an uninstall (which deletes
// it). Shared by the macOS bundle path and the Windows launcher path.
void seedUserDataDirs(const QString &seedRoot);

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
