#!/usr/bin/env bash
# Build a portable Linux AppImage of the Miniscope DAQ.
#
# Mirrors the Windows release recipe (conda env from environment.yml), but builds
# USE_PYTHON=OFF (no embedded Python / DeepLabCut tracker — see BUILD_LINUX.md) so
# the bundle stays lean, then packages it with linuxdeploy + its Qt plugin.
#
# Requirements: an ACTIVE conda env that satisfies the build (Qt6, OpenCV, libuvc,
# cmake, ninja) — i.e. `conda env create -f environment.yml && conda
# activate miniscope-qt6 && conda install -c conda-forge libuvc`. Network access
# to fetch linuxdeploy on first run.
#
# Usage:   conda activate miniscope-qt6 && packaging/linux/build-appimage.sh
# Output:  dist/Miniscope_DAQ[-<version>]-x86_64.AppImage
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$REPO/build-appimage"
APPDIR="$BUILD/AppDir"
TOOLS="$BUILD/tools"
DIST="$REPO/dist"

: "${CONDA_PREFIX:?Activate the conda build env first (conda activate miniscope-qt6)}"
# In CI there is no FUSE; tell the AppImage tools to self-extract instead of mount.
export APPIMAGE_EXTRACT_AND_RUN="${APPIMAGE_EXTRACT_AND_RUN:-1}"

echo "### configure + build (USE_PYTHON=OFF)"
cmake -B "$BUILD" -S "$REPO" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" -DUSE_PYTHON=OFF
cmake --build "$BUILD" -j"$(nproc)"

echo "### fetch linuxdeploy + qt plugin (cached in $TOOLS)"
mkdir -p "$TOOLS"
LD="$TOOLS/linuxdeploy-x86_64.AppImage"
LDQT="$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage"
[ -f "$LD" ]   || curl -sSL -o "$LD"   "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
[ -f "$LDQT" ] || curl -sSL -o "$LDQT" "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
chmod +x "$LD" "$LDQT"

echo "### stage AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/miniscope" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"
cp "$BUILD/MiniscopeDAQ" "$APPDIR/usr/bin/MiniscopeDAQ"        # real ELF (for ldd)
for d in deviceConfigs userConfigs Scripts; do cp -r "$REPO/$d" "$APPDIR/usr/share/miniscope/$d"; done
cp "$REPO/packaging/linux/miniscope-daq.desktop" "$APPDIR/usr/share/applications/miniscope-daq.desktop"
# Square 256x256 app icon, checked in (linuxdeploy requires a standard square size).
cp "$REPO/packaging/linux/miniscope-daq.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/miniscope-daq.png"

echo "### bundle Qt + libs (linuxdeploy + qt plugin)"
export QMAKE="$CONDA_PREFIX/bin/qmake6"
export QML_SOURCES_PATHS="$REPO/source"
export LD_LIBRARY_PATH="$CONDA_PREFIX/lib:${LD_LIBRARY_PATH:-}"
"$LD" --appdir "$APPDIR" \
      --executable "$APPDIR/usr/bin/MiniscopeDAQ" \
      --desktop-file "$APPDIR/usr/share/applications/miniscope-daq.desktop" \
      --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/miniscope-daq.png" \
      --plugin qt

echo "### bundle transitive deps linuxdeploy excludes (OpenCV BLAS, libusb)"
CONDA_LIB="$CONDA_PREFIX/lib"
for _ in 1 2 3 4 5; do
    missing=$(for f in "$APPDIR"/usr/lib/*.so* "$APPDIR"/usr/bin/MiniscopeDAQ.bin; do
                LD_LIBRARY_PATH="$APPDIR/usr/lib" ldd "$f" 2>/dev/null
              done | awk '/not found/{print $1}' | sort -u || true)
    [ -z "$missing" ] && break
    for m in $missing; do
        cand=$(ls "$CONDA_LIB/$m" 2>/dev/null || ls "$CONDA_LIB/${m%.so*}".so* 2>/dev/null | head -1 || true)
        [ -n "$cand" ] && cp -L "$cand" "$APPDIR/usr/lib/$(basename "$m")"
    done
done
cp -L "$CONDA_LIB/libusb-1.0.so.0" "$APPDIR/usr/lib/" 2>/dev/null || true

echo "### install first-run launcher wrapper"
mv "$APPDIR/usr/bin/MiniscopeDAQ" "$APPDIR/usr/bin/MiniscopeDAQ.bin"
cp "$REPO/packaging/linux/AppRun.wrapper" "$APPDIR/usr/bin/MiniscopeDAQ"
chmod +x "$APPDIR/usr/bin/MiniscopeDAQ"

echo "### package AppImage"
mkdir -p "$DIST"
VER="$(sed -nE 's/.*VERSION_NUMBER\s+"([^"]+)".*/\1/p' "$REPO/source/main.cpp" | head -1)"
OUT="Miniscope_DAQ${VER:+-$VER}-x86_64.AppImage"
( cd "$DIST" && ARCH=x86_64 OUTPUT="$OUT" "$LD" --appdir "$APPDIR" --output appimage )
echo "### DONE -> $DIST/$OUT"
ls -lh "$DIST/$OUT"
