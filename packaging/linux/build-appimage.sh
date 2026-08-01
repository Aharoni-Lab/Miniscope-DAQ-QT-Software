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
# Speed rebuilds with ccache when it's on PATH (CI caches its dir across runs);
# a no-op for local builds that don't have ccache installed.
CCACHE_ARGS=()
if command -v ccache >/dev/null 2>&1; then
    CCACHE_ARGS=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi
cmake -B "$BUILD" -S "$REPO" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" -DUSE_PYTHON=OFF "${CCACHE_ARGS[@]}"
cmake --build "$BUILD" -j"$(nproc)"

echo "### fetch linuxdeploy + qt plugin (cached in $TOOLS)"
mkdir -p "$TOOLS"
LD="$TOOLS/linuxdeploy-x86_64.AppImage"
LDQT="$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage"

# An AppImage starts with the ELF magic. Anything else (an HTML/text error page,
# a truncated download) is not a tool we can run.
is_elf() {
    [ -f "$1" ] || return 1
    [ "$(head -c4 "$1" 2>/dev/null | od -An -tx1 | tr -d ' \n')" = "7f454c46" ]
}

# Fetch a tool unless a VALID copy is already cached.
#
# curl needs --fail here. Without it an HTTP error response is written to the
# output file and curl still exits 0, so a transient 404 on the "continuous"
# rolling release got chmod +x'd and executed, failing much later and very
# confusingly with:
#
#   .../linuxdeploy-x86_64.AppImage: line 1: Not: command not found
#
# The ELF check then covers the rest: a cache poisoned before this guard existed
# (locally that file is reused forever, since only its existence was checked), a
# truncated download, or an error page served with a 200.
fetch_tool() {
    local path="$1" url="$2" name
    name="$(basename "$path")"
    if is_elf "$path"; then
        echo "    $name: cached"
        return 0
    fi
    if [ -e "$path" ]; then
        echo "    $name: cached copy is not an executable, refetching"
    fi
    rm -f "$path"
    if ! curl -sSL --fail --retry 3 --retry-delay 5 -o "$path" "$url"; then
        rm -f "$path"
        echo "ERROR: could not download $name from $url" >&2
        exit 1
    fi
    if ! is_elf "$path"; then
        echo "ERROR: $name from $url is not an executable. It starts with:" >&2
        head -c 200 "$path" >&2
        echo >&2
        rm -f "$path"
        exit 1
    fi
    echo "    $name: downloaded"
}

fetch_tool "$LD"   "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
fetch_tool "$LDQT" "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
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

echo "### bundle transitive deps linuxdeploy excludes (OpenCV BLAS, libusb, fontconfig)"
CONDA_LIB="$CONDA_PREFIX/lib"
# NB the executable is still called MiniscopeDAQ at this point - it only becomes
# MiniscopeDAQ.bin when the launcher wrapper is installed further down. Naming
# the .bin here silently skipped the main binary, so this only ever scanned
# usr/lib and never the executable's own dependencies.
for _ in 1 2 3 4 5; do
    missing=$(for f in "$APPDIR"/usr/lib/*.so* "$APPDIR"/usr/bin/MiniscopeDAQ; do
                LD_LIBRARY_PATH="$APPDIR/usr/lib" ldd "$f" 2>/dev/null
              done | awk '/not found/{print $1}' | sort -u || true)
    [ -z "$missing" ] && break
    for m in $missing; do
        cand=$(ls "$CONDA_LIB/$m" 2>/dev/null || ls "$CONDA_LIB/${m%.so*}".so* 2>/dev/null | head -1 || true)
        [ -n "$cand" ] && cp -L "$cand" "$APPDIR/usr/lib/$(basename "$m")"
    done
done
cp -L "$CONDA_LIB/libusb-1.0.so.0" "$APPDIR/usr/lib/" 2>/dev/null || true

# The loop above only recovers libraries ldd reports as "not found". A library
# that IS present on the user's system but is OLDER than the one our bundled libs
# were linked against is invisible to it, and fails at runtime instead:
#
#   MiniscopeDAQ.bin: symbol lookup error: .../libpangoft2-1.0.so.0:
#       undefined symbol: FcConfigSetDefaultSubstitute
#
# linuxdeploy excludes libfontconfig as a "system library", but the conda-forge
# pango we bundle (1.56.x) needs fontconfig >= 2.16 symbols, while Ubuntu 24.04
# ships 2.15. Bundling pango without its matching fontconfig cannot work, so
# bundle fontconfig too.
cp -L "$CONDA_LIB/libfontconfig.so.1" "$APPDIR/usr/lib/" 2>/dev/null || true

echo "### verify the bundle has no unresolved symbols"
# Catches the version-mismatch class of bug above at build time instead of on a
# user's machine. Resolve libraries the way the app will: bundled dir first.
unresolved=""
for f in "$APPDIR"/usr/lib/*.so* "$APPDIR"/usr/bin/MiniscopeDAQ; do
    [ -f "$f" ] || continue
    out=$(LD_LIBRARY_PATH="$APPDIR/usr/lib" ldd -r "$f" 2>&1 || true)
    if grep -q "undefined symbol" <<<"$out"; then
        unresolved+="  $(basename "$f")"$'\n'
        unresolved+="$(grep 'undefined symbol' <<<"$out" | sed 's/^/    /' | head -5)"$'\n'
    fi
done
if [ -n "$unresolved" ]; then
    printf 'ERROR: unresolved symbols in the bundle. A bundled library needs a\n' >&2
    printf '       newer system library than the host provides - bundle it too:\n' >&2
    printf '%s' "$unresolved" >&2
    exit 1
fi

echo "### install first-run launcher wrapper"
mv "$APPDIR/usr/bin/MiniscopeDAQ" "$APPDIR/usr/bin/MiniscopeDAQ.bin"
cp "$REPO/packaging/linux/AppRun.wrapper" "$APPDIR/usr/bin/MiniscopeDAQ"
chmod +x "$APPDIR/usr/bin/MiniscopeDAQ"

echo "### package AppImage"
mkdir -p "$DIST"
VER="$(sed -nE 's/^project\(MiniscopeDAQ\s+VERSION\s+([0-9.]+).*/\1/p' "$REPO/CMakeLists.txt" | head -1)"
OUT="Miniscope_DAQ${VER:+-$VER}-x86_64.AppImage"
( cd "$DIST" && ARCH=x86_64 OUTPUT="$OUT" "$LD" --appdir "$APPDIR" --output appimage )
echo "### DONE -> $DIST/$OUT"
ls -lh "$DIST/$OUT"
