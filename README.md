# DEPRECATION NOTICE

This software is reaching its end of life, and we are in the process of consolidating and rearchitecting miniscope IO software as an SDK with minimal I/O functionality for all miniscopes - [`miniscope-io`](https://github.com/Aharoni-Lab/miniscope-io) and a yet-to-be-started GUI built on top of that. 

This software will receive one final major update to bring it to Qt6 and allow it to be reproducibly built so that current users can maintain it and we can handle minimal patches in the short term. We do not have a firm deprecation timeline yet, but will update this notice with at least 6 months notice before formally dropping support. 

These issues are tracking the update:
- [ ] [Reproducible Builds](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/issues/61)
- [ ] [Update to Qt6](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/issues/62)

# Miniscope-DAQ-QT-Software

**[[Miniscope V4 Wiki](https://github.com/Aharoni-Lab/Miniscope-v4/wiki)] [[Miniscope DAQ Software Wiki](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/wiki)] [[Miniscope DAQ Firmware Wiki](https://github.com/Aharoni-Lab/Miniscope-DAQ-Cypress-firmware/wiki)] [[Miniscope Wire-Free DAQ Wiki](https://github.com/Aharoni-Lab/Miniscope-Wire-Free-DAQ/wiki)] [[Miniscope-LFOV Wiki](https://github.com/Aharoni-Lab/Miniscope-LFOV/wiki)][[2021 Virtual Miniscope Workshop Recording](https://sites.google.com/metacell.us/miniscope-workshop-2021)]**

Neural and behavior control and recording software for the UCLA Miniscope Project

**Make sure to click Watch and Star in the upper right corner of this page to get updates on new features and releases.**

<p align="center">
  <img width="600" src="https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/blob/master/wikiImg/miniscope_bright.png ">
</p>


This backwards compatible Miniscope DAQ software is an upgrade and replacement to our previous DAQ software. It is written completely in QT, using C++ for the backend and QT Quick (QML and Java) for the front end. This software supports all current and previous Miniscopes but requires the most up-to-date version of the [Miniscope DAQ Firmware](https://github.com/Aharoni-Lab/Miniscope-DAQ-Cypress-firmware).

**All information can be found on the [Miniscope DAQ Software Wiki page](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/wiki).**

Along with this repository holding the software's source code, you can get built release versions of the software on the [Releases Page](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/releases).

## Installation (Windows)

Each release on the [Releases Page](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/releases) ships in two forms — pick whichever suits you:

- **Installer — `MiniscopeDAQ-<version>-Setup.exe`** (recommended). A standard Windows setup wizard. It installs per-user (no administrator rights required), adds a Start Menu shortcut (and an optional desktop shortcut), and registers an entry under *Add or Remove Programs* so it can be uninstalled cleanly.
- **Portable zip — `MiniscopeDAQ-<version>-Windows-x64.zip`**. Unzip it anywhere and double-click `MiniscopeDAQ.exe` — nothing is installed, and you can remove it just by deleting the folder. Handy when you can't run an installer or want to carry it on a USB drive.

Both are fully self-contained: all dependencies (Qt, OpenCV, Python, …) are bundled, so no separate installation is needed.

## Installation (Linux)

A portable **AppImage** is published on the [Releases Page](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/releases): `Miniscope_DAQ-<version>-x86_64.AppImage`. It bundles Qt, OpenCV, libuvc and libusb, so nothing else needs installing (tested on Ubuntu 24.04).

```bash
chmod +x Miniscope_DAQ-*-x86_64.AppImage
./Miniscope_DAQ-*-x86_64.AppImage
```

> On GNOME, double-clicking an AppImage may do nothing — either run it from a terminal as above, or create a launcher / use [AppImageLauncher](https://github.com/TheAssassin/AppImageLauncher).

**First run** asks where to keep your user-config files and your recordings (defaulting to `~/Documents/Miniscope/…`). To choose again later, delete `~/.config/MiniscopeDAQ/paths.conf`.

**USB permissions.** On a normal desktop login the Miniscope is accessible out of the box. For headless/remote sessions (or if you hit a USB access error), install the bundled udev rule once:

```bash
sudo cp packaging/linux/99-miniscope.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger   # then re-plug the Miniscope
```

> **Note on the Linux build.** The Miniscope is driven directly via **libuvc** on Linux (the kernel `uvcvideo` driver caches UVC control reads, so it can't return the live frame counter or BNO head-orientation data). The DeepLabCut-Live behavior tracker is **not** included in the AppImage. To build from source (and to enable the tracker with `-DUSE_PYTHON=ON`), see [`BUILD_LINUX.md`](BUILD_LINUX.md).
