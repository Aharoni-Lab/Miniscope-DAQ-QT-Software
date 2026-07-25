# Building & running the Miniscope DAQ on macOS (Apple Silicon)

Status: **fully working and bench-validated on real Miniscope hardware**
(stock V4 + standard DAQ, Apple Silicon, July 2026): native streaming pinned
to the scope's USB identity, LED/gain/EWL/frame-rate control with confirmed
effect on the sensor, live DAQ frame counter with zero drops over multi-minute
runs, BNO head-orientation traces, and dual-device capture alongside a USB
webcam. See "Port status" below for the per-area detail.

The C++/CMake were already cross-platform: every Windows- or Linux-specific bit
is gated behind `if(WIN32)` / `UNIX AND NOT APPLE` (CMake) or
`#ifdef Q_OS_WINDOWS` / `Q_OS_LINUX` (C++), each with a sane generic fallback,
so macOS compiles the portable paths as-is.

---

## 1. Toolchain: Miniforge (conda-forge)

Use the repo's `environment.yml` — it pins the same Qt/OpenCV/Python stack as
the Windows and Linux CI builds to major.minor (Qt 6.11, OpenCV 4.13,
Python 3.12), floating only the patch level, and every pin resolves on
`osx-arm64` unchanged. For the exact package set of a released version, use
`conda-lock.yml` (see RELEASING.md).

Prerequisites: Xcode Command Line Tools (`xcode-select --install`; AppleClang
is the compiler — no full Xcode needed).

```bash
# Miniforge (user-local, no sudo), if you don't already have conda:
curl -fsSL -o Miniforge3.sh \
  https://github.com/conda-forge/miniforge/releases/latest/download/Miniforge3-MacOSX-arm64.sh
bash Miniforge3.sh -b -p "$HOME/miniforge3"

# Create the build env:
"$HOME/miniforge3/bin/conda" env create -f environment.yml
source "$HOME/miniforge3/bin/activate" miniscope-qt6
```

- [ ] `conda activate miniscope-qt6` works
- [ ] `cmake --version` and `ninja --version` resolve from the env

---

## 2. Configure & build

```bash
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" \
  -DPython3_EXECUTABLE="$CONDA_PREFIX/bin/python3" -DUSE_PYTHON=ON
cmake --build build
```

`USE_PYTHON=OFF` also builds if you don't need the DeepLabCut-Live behavior
tracker (the packaged Linux AppImage already ships that way).

- [ ] Configure finds Qt6, OpenCV, and (if `USE_PYTHON=ON`) Python3+NumPy
- [ ] Build completes; `build/MiniscopeDAQ.app` exists

---

## 3. Run

The build produces a `.app` bundle (required on macOS: the camera-permission
prompt is attributed to the bundle and its `NSCameraUsageDescription`; a bare
executable gets killed or silently denied when it touches the camera).

```bash
open build/MiniscopeDAQ.app        # or run the binary inside it directly:
./build/MiniscopeDAQ.app/Contents/MacOS/MiniscopeDAQ
```

At launch the app copies its internal configs (`deviceConfigs`, `Scripts`)
from the bundle into `~/Library/Application Support/Miniscope DAQ` (refreshed
every launch) and works from there, and seeds the example user configs into
`~/Documents/Miniscope/userConfigs` (never overwriting your edits — same
contract as the Linux AppImage). Recordings default to
`~/Documents/Miniscope/data`. Override either folder with the
`MINISCOPE_USERCONFIG_DIR` / `MINISCOPE_DATA_DIR` environment variables.

Qt/QML diagnostics print to the terminal. `QSG_INFO=1` in the environment shows
the scene-graph setup — expect an **OpenGL 2.1 context** (the legacy
compatibility context; exactly what the custom GLSL 1.10 shaders need) and the
`basic` (single-threaded) render loop, which is normal for OpenGL on macOS.

- [ ] Main window launches and renders

---

## 4. Packaging: distributable .app + DMG

```bash
conda activate miniscope-qt6
packaging/macos/build-dmg.sh       # -> dist/Miniscope-DAQ-<ver>-macOS-arm64.dmg
```

The script does its own `USE_PYTHON=OFF` build (no embedded Python /
DeepLabCut tracker — matching the Linux AppImage), bundles Qt + OpenCV +
plugins + QML into the `.app` with `macdeployqt`, ad-hoc signs it, and wraps
it in a DMG. The release CI runs the same script, so a tagged release ships
Windows + Linux + macOS together.

**First launch on another Mac:** the bundle is ad-hoc signed, not notarized,
so Gatekeeper warns about an unidentified developer. Right-click the app >
Open > Open (needed once). If macOS claims the app "is damaged", clear the
quarantine flag instead: `xattr -cr /Applications/MiniscopeDAQ.app`.
Proper Developer ID signing + notarization can be added to the script later
without changing anything else.

**iPhone / Continuity Camera interference (bench-verified):** a nearby iPhone
joins the Mac's camera list via Continuity Camera and macOS actively promotes
it, which can steal the video session away from the Miniscope (symptom: the
scope "connects" but the video window shows the phone, freezes after ~1 s, or
never streams — while BNO/controls keep working, since they use a separate
USB channel bound to the scope). For recording rigs, disable it: on the
iPhone, Settings > General > AirPlay & Continuity > Continuity Camera off
(or move the phone away). The app re-binds the scope's video stream to its
USB identity when the camera list shifts, but macOS's automatic camera
selection can still interfere at session start.

---

## 5. Port status — what works, what doesn't (yet)

| Area | Status |
|---|---|
| Build (CMake + conda, arm64) | ✅ works |
| Main window / QML UI / OpenGL shaders | ✅ verified (GL 2.1, all 7 shader programs compile+link) |
| Recording codecs (FFV1, GREY via FFmpeg) | ✅ reported supported |
| Scan Devices button | ✅ AVFoundation enumeration (index == deviceID; Miniscopes called out) |
| **Miniscope control transport (IOKit pipe-0)** | ✅ bench-validated on a real Miniscope (LED/gain/EWL/frame rate, DAQ counter, BNO) |
| **Miniscope capture backend (`VideoStreamMac` hybrid)** | ✅ bench-validated: uniqueID-pinned stream, zero frame drops, survives iPhone/webcam hot-plugs and USB-drop reconnects |
| Behavior webcams (OpenCV → AVFoundation) | ✅ `.app` bundle gives proper camera-permission attribution (`NSCameraUsageDescription`) |
| Packaged `.app` / DMG | ✅ `packaging/macos/build-dmg.sh`, wired into release CI (ad-hoc signed; right-click → Open) |

**Why Miniscope control needs macOS-specific work.** The Miniscope smuggles its
control channel through UVC Processing-Unit controls (I²C commands out via
`CONTRAST/GAMMA/SHARPNESS` writes; frame counter and BNO quaternion back via
`GET_CUR` reads). Neither existing backend can do this on macOS:

* **OpenCV property passthrough (the Windows path)**: OpenCV's AVFoundation
  backend implements only width/height/fps — macOS's `AVCaptureDevice` API has
  no gain/contrast/hue/etc. properties to plumb through at all.
* **libuvc (the Linux path)**: since macOS 12, Apple's UVC driver stack claims
  every UVC device exclusively; detaching it via libusb requires **root** on
  every launch (that is how libuvc-based apps like Pupil Capture ship on
  macOS). Not acceptable here.

**The fix** (implemented, bench-validated) is a hybrid backend: frames stream
through a native AVFoundation capture session pinned to the scope's stable
`uniqueID` (`avfframegrabbermac.mm` — not OpenCV, whose index-based opening
binds the wrong camera when the device list shifts), while a small IOKit
module sends the UVC `SET_CUR`/`GET_CUR` requests over **the default control
pipe (pipe 0) of the VideoControl interface, without opening the interface** —
which Apple's driver leaves available while it streams (an Apple-DTS-sanctioned
pattern, used by openpnp-capture among others; requires no root and no special
entitlements). Unlike Linux's `uvcvideo`, there is no kernel-side control cache
in this path, so `GET_CUR` reads are live by construction.
