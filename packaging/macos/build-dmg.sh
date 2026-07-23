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
cmake -B "$BUILD" -S "$REPO" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" -DUSE_PYTHON=OFF -DBUILD_TESTING=OFF
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
