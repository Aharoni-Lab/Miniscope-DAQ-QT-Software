#!/usr/bin/env bash
# Build a distributable macOS .app + DMG of the Miniscope DAQ (Apple Silicon).
#
# Mirrors the Linux AppImage recipe (packaging/linux/build-appimage.sh): a conda
# env from environment.yml, built USE_PYTHON=OFF (no embedded Python /
# DeepLabCut tracker) so the bundle stays lean, then packaged with macdeployqt
# (Qt frameworks + plugins + QML + the OpenCV dylibs all end up inside the
# .app) and wrapped in a compressed DMG.
#
# Signing: the bundle is AD-HOC signed (`codesign -s -`). Apple Silicon refuses
# to run unsigned binaries at all, so this is mandatory; it does NOT satisfy
# Gatekeeper for downloaded apps - first launch needs right-click > Open (see
# BUILD_MACOS.md). Proper Developer ID signing + notarization can replace the
# ad-hoc step later without touching anything else here.
#
# Usage:   conda activate miniscope-qt6 && packaging/macos/build-dmg.sh
# Output:  dist/Miniscope-DAQ-<version>-macOS-<arch>.dmg
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$REPO/build-dmg"
DIST="$REPO/dist"

: "${CONDA_PREFIX:?Activate the conda build env first (conda activate miniscope-qt6)}"
# conda's Qt puts the deploy tools in lib/qt6/bin, not on PATH.
MACDEPLOYQT="$CONDA_PREFIX/lib/qt6/bin/macdeployqt"
[ -x "$MACDEPLOYQT" ] || MACDEPLOYQT="$(command -v macdeployqt)"

echo "### configure + build (USE_PYTHON=OFF)"
# Speed rebuilds with ccache when it's on PATH (CI caches its dir across runs,
# and this DMG build reuses objects from the test build earlier in the job); a
# no-op for local builds that don't have ccache installed.
CCACHE_ARGS=()
if command -v ccache >/dev/null 2>&1; then
    CCACHE_ARGS=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi
# The ${arr[@]+"${arr[@]}"} form is required for macOS's stock bash 3.2, where
# expanding an EMPTY array under `set -u` is an "unbound variable" error (bash
# only fixed that in 4.4). Hit whenever ccache isn't installed, i.e. every fresh
# local Mac; CI has ccache, so the array is non-empty there and it never fires.
cmake -B "$BUILD" -S "$REPO" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" -DUSE_PYTHON=OFF -DBUILD_TESTING=OFF \
      ${CCACHE_ARGS[@]+"${CCACHE_ARGS[@]}"}
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"

APP="$BUILD/MiniscopeDAQ.app"

echo "### bundle Qt + libs (macdeployqt)"
# -qmldir scans the QML sources (they ship inside qrc) for imports so the
# needed QML modules land in Contents/Resources/qml.
"$MACDEPLOYQT" "$APP" -qmldir="$REPO/source"

echo "### ad-hoc codesign"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

echo "### package DMG"
mkdir -p "$DIST"
VER="$(sed -nE 's/^project\(MiniscopeDAQ[[:space:]]+VERSION[[:space:]]+([0-9.]+).*/\1/p' "$REPO/CMakeLists.txt" | head -1)"
ARCH="$(uname -m)"
OUT="Miniscope-DAQ${VER:+-$VER}-macOS-$ARCH.dmg"
STAGE="$BUILD/dmg-stage"
rm -rf "$STAGE" "$DIST/$OUT"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
hdiutil create -volname "Miniscope DAQ" -srcfolder "$STAGE" -ov -format UDZO "$DIST/$OUT"

echo "### DONE -> $DIST/$OUT"
ls -lh "$DIST/$OUT"
