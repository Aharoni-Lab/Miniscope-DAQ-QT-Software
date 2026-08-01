# Releasing

How a Miniscope DAQ release is cut. One tag produces the Windows portable zip,
the Windows installer (`Setup.exe`), the Linux AppImage, and the macOS DMG
together from CI (`.github/workflows/release.yml`). All platforms build, test,
and ship the same `USE_PYTHON=OFF` configuration (no embedded DeepLabCut-Live).

## Steps

1. **Bump the version.** Set `project(MiniscopeDAQ VERSION x.y.z)` in
   `CMakeLists.txt` - the single source of truth. The in-app version (via
   `MINISCOPE_VERSION`), the DMG/AppImage file names, and the Windows installer
   all derive from it, and CI's `check-version` job **fails the release unless
   the git tag equals this value**, so the tag and every artifact can never
   disagree. Releases are 3-part SemVer (`vX.Y.Z`) from v2.0.0 on.

2. **Finalize the changelog.** Rename the `[Unreleased]` header in
   `CHANGELOG.md` to `[x.y.z] — YYYY-MM-DD`. The CI `release` job extracts this
   section verbatim and uses it as the GitHub Release notes, so this is exactly
   what users read - write it for them, not as an internal PR log.

3. **Freeze the dependency set.** Regenerate the lock file so the release's
   exact library versions are recorded:

   ```sh
   pip install conda-lock
   conda-lock lock -f environment.yml -p win-64 -p linux-64 -p osx-arm64 \
       --virtual-package-spec packaging/conda-virtual-packages.yml \
       --lockfile conda-lock.yml
   ```

   The `--virtual-package-spec` file tells the solver to assume macOS 12.0
   (the app's real minimum); without it the osx-arm64 solve fails on
   packages that require `__osx >= 12.0` (see the file's header comment).

   Commit the updated `conda-lock.yml`. (Day-to-day builds and CI use
   `environment.yml`, which floats on patch releases within its major.minor
   pins; the lock file is the snapshot of record.)

   Known gap: `libuvc` (Linux Miniscope backend) is installed by the Linux CI
   job separately from `environment.yml`, so it is not in the lock file. The
   Linux job now attaches a `versions-linux-x86_64.txt` (a full `conda list`,
   including the resolved `libuvc`) to the release, so the exact version
   survives even after the CI logs expire.

4. **Merge to master and tag.** Merge the release branch with a REGULAR merge
   (not squash - the PR history is the history), then tag:

   ```sh
   git tag vX.Y.Z && git push origin vX.Y.Z
   ```

   CI runs `check-version`, builds/tests all platforms, and cuts a **DRAFT**
   GitHub Release with all four artifacts + the Linux versions file. A tag with
   a pre-release suffix (e.g. `v2.0.0-rc1`) is marked as a prerelease rather
   than "Latest". If `check-version` fails, the tag disagrees with
   `CMakeLists.txt`: fix step 1, delete and re-push the tag.

5. **Sanity-check the artifacts, then publish.** The release is a draft, so
   nothing is public yet. Download each artifact and confirm it launches and can
   load an example config. On macOS also confirm the DMG opens on a machine that
   didn't build it (ad-hoc signature; on current macOS this needs
   System Settings → Privacy & Security → "Open Anyway", or
   `xattr -cr /Applications/MiniscopeDAQ.app` - see BUILD_MACOS.md). When
   satisfied, click **Publish release** on GitHub.

## Rebuilding an old release exactly

```sh
git checkout vX.Y.Z
conda-lock install -n miniscope-rebuild conda-lock.yml
conda activate miniscope-rebuild
# then build per BUILD_MACOS.md / BUILD_LINUX.md / environment.yml header
# for Linux, also install the libuvc version from that release's
# versions-linux-x86_64.txt asset (it is not in conda-lock.yml).
```

Note: this reproduces the dependency set, not a byte-identical binary - the
build embeds its compile date, and the CI runner toolchain/system libs
(MSVC/Xcode/AppleClang, apt GL libs, Inno Setup, linuxdeploy) float with the
runner images. Treat it as "same versions," not "same bytes."
