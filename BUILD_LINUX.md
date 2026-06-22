# Building the Miniscope DAQ on Ubuntu (Linux port)

Checklist for getting the Qt6 build compiling, launching, and running on Ubuntu
(targeting **Ubuntu 24.04 LTS**, which ships Qt 6.4 + OpenCV 4.6). Work happens on
the **`ubuntu-port`** branch.

The C++ and CMake are already cross-platform: every Windows-specific bit is gated
behind `if(WIN32)` (CMake) or `#ifdef Q_OS_WINDOWS` (C++), and the OpenGL renderers
use legacy GLSL that compiles against Linux's OpenGL compatibility profile with no
changes. So this is mostly "install deps, configure, build."

---

## Decision: system packages (apt) vs. conda

**Use apt (recommended).** On Linux the *system* Qt links natively against the
distro's OpenGL / X11 / Wayland / V4L2 stack, which is exactly what this app needs
(raw OpenGL rendering + camera capture). Conda's Qt on Linux is prone to `xcb`
platform-plugin and `libGL`/`libstdc++` conflicts — the opposite of the Windows
situation, where conda was the pragmatic choice. apt is also the natural path toward
a `.deb` later.

Conda remains a **fallback** (see bottom) — `environment.yml` is in the repo and is
conda-forge, so it resolves on Linux too.

---

## 1. Clone & checkout

```bash
git clone https://github.com/fnsangiul/Miniscope-DAQ-QT-Software.git
cd Miniscope-DAQ-QT-Software
git checkout ubuntu-port
```

- [ ] On branch `ubuntu-port`
- [ ] `git status` clean

---

## 2. Install build dependencies

**Core toolchain + Qt6 + OpenCV + Python** (high-confidence package names on 24.04):

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake git pkg-config \
  qt6-base-dev qt6-declarative-dev \
  libgl1-mesa-dev \
  libopencv-dev \
  python3-dev python3-numpy \
  libxcb-cursor0 v4l-utils
```

**QML runtime modules** (separate command so a name typo doesn't block the core
install). These are needed at *runtime* for the QML UI to load:

```bash
sudo apt install -y \
  qml6-module-qtquick qml6-module-qtquick-window \
  qml6-module-qtquick-controls qml6-module-qtquick-templates \
  qml6-module-qtquick-layouts qml6-module-qtquick-dialogs \
  qml6-module-qtqml-workerscript
```

- [ ] Core deps installed
- [ ] QML modules installed

> If a `qml6-module-*` name is rejected, run `apt search qml6-module` to find the
> exact name on your release. You can also defer this — at launch the app prints the
> *exact* missing import (e.g. `module "QtQuick.Dialogs" is not installed`), so you
> can install precisely what it asks for.

---

## 3. Configure & build

Unlike the Windows recipe (VS multi-config, explicit `CMAKE_PREFIX_PATH`), apt's Qt6
and OpenCV are auto-discovered by CMake. Single-config generator, so set the build
type explicitly:

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DUSE_PYTHON=ON
cmake --build build -j"$(nproc)"
```

- [ ] CMake configure finds Qt6, OpenCV, and Python3 + NumPy (check the configure log)
- [ ] Build completes

> **Keep `USE_PYTHON=ON` for the first build.** `USE_PYTHON=OFF` does *not* compile
> yet — `PyObject` members in `behaviortrackerworker.h` aren't `#ifdef`-guarded.
> `python3-dev` + `python3-numpy` are enough to **compile and launch**; you only need
> `deeplabcut-live` (pip) if you actually run the behavior tracker.
> *(Optional cleanup task for this branch: guard those members so `USE_PYTHON=OFF`
> builds — then the tracker becomes truly opt-in.)*

---

## 4. Run

Single-config build puts the binary directly under `build/` (not `build/Release/`):

```bash
# locate it if unsure:
find build -name MiniscopeDAQ -type f -executable

# run from the REPO ROOT so it finds ./deviceConfigs ./userConfigs ./Scripts:
./build/MiniscopeDAQ
```

The app reads `./deviceConfigs`, `./userConfigs`, `./Scripts` **relative to the
current directory** (backend.cpp), and those folders live at the repo root — so run
from the repo root.

- [ ] Main window launches and renders (button styling, device/codec lists populate)
- [ ] Load a user config → device windows open
- [ ] Click **Run** → live video renders (this exercises the OpenGL renderers)
- [ ] Trace window plots (BNO / neuron traces)

> On Linux a GUI app prints Qt/QML diagnostics straight to the terminal — no
> `QT_FORCE_STDERR_LOGGING` needed. Watch the terminal for missing-module or
> shader-compile messages.

---

## 5. Expected first-failure points (and fixes)

| Symptom | Fix |
|---|---|
| `module "QtQuick.X" is not installed` at launch | Install `qml6-module-qtquick-<x>` (lower-cased, dashed). |
| `Could not load the Qt platform plugin "xcb"` | `libxcb-cursor0` (in the list above). Still failing? Force X11: `QT_QPA_PLATFORM=xcb ./build/MiniscopeDAQ`. |
| Black window / no GL / Wayland weirdness | Ubuntu 24.04 defaults to Wayland; try `QT_QPA_PLATFORM=xcb` (runs via XWayland). Verify GL with `glxinfo \| grep "OpenGL version"` (`sudo apt install mesa-utils`). |
| CMake can't find NumPy | `python3-numpy`; confirm `find_package(Python3 ... NumPy)` succeeds in the configure log. |
| Camera not found / permission denied | `ls -l /dev/video*`; ensure your user is in the `video` group (`groups`; if not: `sudo usermod -aG video $USER` then re-login). List devices: `v4l2-ctl --list-devices`. |
| **Scan Devices** button says "only on Windows" | Expected — that button is `#ifdef Q_OS_WINDOWS` (DirectShow). On Linux, set the `deviceID` manually (0, 1, …; map names via `v4l2-ctl --list-devices`). *(Optional enhancement: add a V4L2 enumeration path.)* |
| Shaders fail to compile (blank trace/tracker) | Already fixed in this codebase (GLSL 1.10 int→float issues). If it recurs, grep the terminal log for shader compile errors. |

---

## 6. The real test: Miniscope control over V4L2 (hardware)

Even once it compiles and the UI runs, the **one genuine runtime unknown** is whether
Miniscope control works. The app tunnels device control through standard UVC video
properties (`cap.set(cv::CAP_PROP_SATURATION/GAMMA/CONTRAST, …)`). On Windows that
goes through DirectShow; on Linux OpenCV's **V4L2** backend may not forward those
arbitrary UVC property writes the same way. This needs **hands-on testing with the
actual Miniscope** and is likely harder than the build itself. Verify excitation LED,
gain, focus, and framerate actually change the device.

---

## 7. Packaging (later, after it runs)

- **AppImage** (recommended first): `linuxdeployqt` or `linuxdeploy` + the Qt plugin —
  bundles Qt/OpenCV/plugins/QML, portable across distros.
- **`.deb`**: CPack `DEB` generator or a `debian/` dir with `Depends:` on
  `libqt6*`, `libopencv*`, `python3` — smaller, but couples to one release's versions.

---

## Fallback: conda build

If apt's Qt 6.4 hits a version wall:

```bash
conda env create -f environment.yml   # creates the miniscope-qt6 env (conda-forge)
conda activate miniscope-qt6
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" \
  -DPython3_EXECUTABLE="$CONDA_PREFIX/bin/python3" -DUSE_PYTHON=ON
cmake --build build -j"$(nproc)"
```

Watch for the conda-Qt `xcb` plugin issue: if the platform plugin won't load, you may
need `QT_QPA_PLATFORM_PLUGIN_PATH="$CONDA_PREFIX/lib/qt6/plugins/platforms"` and/or
system `libxcb-cursor0`. This is exactly the friction that makes apt the better first
choice on Linux.
