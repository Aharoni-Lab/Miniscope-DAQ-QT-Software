# Releasing

How a Miniscope DAQ release is cut. One tag produces the Windows portable zip,
the Windows installer (`Setup.exe`), the Linux AppImage, and the macOS DMG
together from CI (`.github/workflows/release.yml`).

## Steps

1. **Freeze the dependency set.** Regenerate the lock file so the release's
   exact library versions are recorded and the binaries can be rebuilt
   identically later, even after conda-forge moves on:

   ```sh
   pip install conda-lock
   conda-lock lock -f environment.yml -p win-64 -p linux-64 -p osx-arm64 \
       --lockfile conda-lock.yml
   ```

   Commit the updated `conda-lock.yml`. (Day-to-day builds use
   `environment.yml`, which floats on patch releases within its major.minor
   pins; the lock file is the snapshot of record.)

   Known gap: `libuvc` (Linux Miniscope backend) is installed by the Linux CI
   job separately from `environment.yml`, so it is not in the lock file. When
   rebuilding a Linux release exactly, check the release job's log for the
   libuvc version it installed.

2. **Finalize the changelog.** Rename the `[Unreleased]` section in
   `CHANGELOG.md` to the version number and date.

3. **Check the version.** `project(MiniscopeDAQ VERSION x.y.z)` in
   `CMakeLists.txt` is the single source of truth - the UI, the DMG/AppImage
   file names, and the installer all derive from it.

4. **Merge to master and tag.** Merge the release branch with a REGULAR merge
   (not squash - the PR history is the history), then tag:

   ```sh
   git tag vX.Y.Z && git push origin vX.Y.Z
   ```

   CI builds and attaches all four artifacts to the GitHub release.

5. **Sanity-check the artifacts.** Download each artifact and confirm it
   launches and can load an example config. On macOS also confirm the DMG
   opens on a machine that didn't build it (ad-hoc signature, Gatekeeper
   right-click-Open path).

## Rebuilding an old release exactly

```sh
git checkout vX.Y.Z
conda-lock install -n miniscope-rebuild conda-lock.yml
conda activate miniscope-rebuild
# then build per BUILD_MACOS.md / BUILD_LINUX.md / environment.yml header
```
