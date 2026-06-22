# Building & running the Miniscope DAQ on Ubuntu (Linux port)

Status: **working on Ubuntu 24.04 LTS** — builds, launches (Wayland or X11),
streams Miniscope + webcam video, records, all device controls work, **and the
BNO head-orientation traces + frame counter work** (see the libuvc section
below for why that took special handling). Work happens on the **`ubuntu-port`**
branch.

The C++/CMake were already cross-platform: every Windows-specific bit is gated
behind `if(WIN32)` (CMake) or `#ifdef Q_OS_WINDOWS` (C++), and the OpenGL
renderers use legacy GLSL that compiles against Linux's GL compatibility profile
unchanged. The two Linux-specific additions made for this port are:

1. The CMake Qt floor was lowered `6.5 → 6.4` (harmless; satisfied by any newer Qt).
2. A **libuvc capture backend** for Miniscopes (`VideoStreamLibUVC`), because the
   kernel `uvcvideo` driver caches UVC control reads — see below.

---

## Toolchain: conda (validated) vs. apt

**Use the conda environment (`environment.yml`).** It pins the *exact* same
Qt/OpenCV/Python as the Windows CI build (Qt 6.11, OpenCV 4.13, Python 3.12), and
it has been validated end-to-end on Ubuntu 24.04 — including launching natively on
**Wayland** with no `xcb`/`libGL` friction (the historical conda-Qt-on-Linux
worry did **not** materialize here).

apt's Qt 6.4 / OpenCV 4.6 should also work now that the CMake floor is 6.4, and is
the natural path toward a `.deb`, but conda is the tested path documented here.

---

## 1. Clone & checkout

```bash
git clone https://github.com/fnsangiul/Miniscope-DAQ-QT-Software.git
cd Miniscope-DAQ-QT-Software
git checkout ubuntu-port
```

---

## 2. Create the build environment

```bash
conda env create -f environment.yml      # creates miniscope-qt6 (conda-forge)
conda activate miniscope-qt6
conda install -c conda-forge libuvc      # Linux Miniscope capture backend (see below)
```

`libuvc` (and its `libusb-1.0` dependency) are needed for the Miniscope on Linux.
If libuvc is absent, the app still builds and runs, but Miniscopes fall back to the
OpenCV/V4L2 backend (no live BNO / frame counter — see below).

- [ ] `conda activate miniscope-qt6` works
- [ ] `libuvc` present (`ls $CONDA_PREFIX/lib/libuvc.so`)

---

## 3. Configure & build

apt/conda Qt6 + OpenCV are auto-discovered. Single-config generator, so set the
build type explicitly. Point CMake at the conda env:

```bash
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" \
  -DPython3_EXECUTABLE="$CONDA_PREFIX/bin/python" -DUSE_PYTHON=ON
cmake --build build -j"$(nproc)"
```

Look for `libuvc found (...); Miniscope Linux capture backend enabled` in the
configure log. Keep `USE_PYTHON=ON` (the DeepLabCut tracker's `PyObject` members
in `behaviortrackerworker.h` aren't `#ifdef`-guarded, so `OFF` doesn't compile yet).

- [ ] Configure finds Qt6, OpenCV, Python3+NumPy, **and libuvc**
- [ ] Build completes; `build/MiniscopeDAQ` exists

---

## 4. Run

```bash
# run from the REPO ROOT so it finds ./deviceConfigs ./userConfigs ./Scripts:
./build/MiniscopeDAQ
```

Qt/QML diagnostics print straight to the terminal. For the Miniscope you should
see `Using libuvc capture backend for "<name>"`.

- [ ] Main window launches and renders
- [ ] Load a user config → device windows open → **Run** → live video renders
- [ ] (V4-BNO) roll/pitch/yaw traces plot and move when the scope is rotated

---

## 5. Picking the right `deviceID` (Linux)

On Linux a config's `deviceID` is the OpenCV/V4L2 index = `/dev/video<deviceID>`.
A single UVC camera usually exposes **two** `/dev/videoN` nodes — a capture node
and a metadata node — so the right index is not always obvious. Map them with:

```bash
for d in /sys/class/video4linux/video*; do echo "$(basename $d): $(cat $d/name)"; done
```

Pick the **capture** node for each device. Example seen during this port:
`/dev/video0` = webcam capture, `/dev/video1` = webcam metadata (not capturable),
`/dev/video2` = **Miniscope capture**, `/dev/video3` = Miniscope metadata. So the
Miniscope needed `deviceID: 2`, not `1`.

> The **Scan Devices** button is Windows-only (DirectShow, `#ifdef Q_OS_WINDOWS`);
> set `deviceID` manually on Linux.

---

## 6. The libuvc Miniscope backend (the important part)

**Why it exists.** The Miniscope streams data *back* to the host by overloading
UVC Processing-Unit controls: the DAQ frame counter is read from `CONTRAST`, the
BNO head-orientation quaternion from `SATURATION/HUE/GAIN/BRIGHTNESS`, and the
external-trigger state from `GAMMA`. On Windows, DirectShow issues a fresh
`GET_CUR` on every read, so these always return live values. On **Linux**, the
kernel `uvcvideo` driver **caches** UVC control values and only re-queries the
device on a *write* — so OpenCV/V4L2 `cap.get()` returns stale data: the BNO never
updates (registers are never written), and the frame counter freezes between
control writes (its lost-frame counter then runs away negative).

**The fix.** For Miniscopes on Linux the app uses `VideoStreamLibUVC` instead of
OpenCV. libuvc talks to the device directly over libusb (detaching `uvcvideo`
while open) and issues a fresh UVC `GET_CUR` on every read, bypassing the cache.
It also handles streaming (YUYV 608×608) and all control *writes* (LED/gain/EWL/
framerate) via the same I²C-over-UVC packing the OpenCV path used. Backend
selection lives in `VideoDevice`'s constructor: Linux + Miniscope + a live
`deviceID` → libuvc; everything else (webcams, Windows, video-file playback) →
OpenCV. Both backends implement the `VideoStreamBase` interface, so the rest of
the app is unchanged and the Windows/OpenCV path is untouched.

**One behavioral note.** Continuous BNO/frame-register refresh on the DAQ is
enabled by a `SET_CUR SATURATION = 0x0001` "start" command. The app wires that to
the **Record** button; the libuvc backend instead sends it at **stream start** so
head orientation is live during **Run**, not only while recording.

**Permissions.** libusb needs access to `/dev/bus/usb/...`; on a desktop Ubuntu
session this is granted to the logged-in user automatically (no udev rule/root
needed). If you run headless or hit `LIBUSB_ERROR_ACCESS`, add a udev rule for
VID:PID `04b4:00f9` (Cypress FX3 / Miniscope).

---

## 7. Troubleshooting

| Symptom | Fix |
|---|---|
| `module "QtQuick.X" is not installed` at launch | Missing QML runtime module — with the conda env this shouldn't happen; on apt install `qml6-module-qtquick-<x>`. |
| `Could not load the Qt platform plugin "xcb"` | Force X11: `QT_QPA_PLATFORM=xcb ./build/MiniscopeDAQ` (validated build used Wayland fine). |
| `libuvc not found` at configure | `conda install -c conda-forge libuvc`; re-run cmake. Without it Miniscopes lose live BNO / frame counter. |
| Miniscope opens but no BNO / frozen orientation | Confirm the log says `Using libuvc capture backend`; confirm device is a `Miniscope_V4_BNO` and `headOrientation.enabled` is true in the user config. |
| `could not open ... via libuvc (device busy?)` | Another process holds the device; close other viewers. After libuvc closes, `uvcvideo` re-attaches and `/dev/videoN` returns. |
| Wrong/te no video | Wrong `deviceID` — re-check the `/sys/class/video4linux` mapping in §5. |

---

## 8. Packaging (later)

- **AppImage** (`linuxdeploy` + Qt plugin) — bundle Qt/OpenCV/libuvc/libusb/QML.
- **`.deb`** — CPack `DEB` or a `debian/` dir depending on `libqt6*`, `libopencv*`,
  `libuvc0`, `libusb-1.0-0`, `python3`.

Either way, ship a udev rule for `04b4:00f9` so non-desktop sessions get USB access.
