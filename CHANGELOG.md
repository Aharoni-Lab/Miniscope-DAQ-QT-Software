# Changelog

All notable user-facing changes to the Miniscope DAQ software are documented
here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

Every pull request that changes behavior should add a line to the
**[Unreleased]** section below; the section is renamed to the version number
when a release is tagged.

## [Unreleased] — v2.0.0, first release of the Qt 6 generation

This is one final major release of the Qt desktop software before it is
superseded long-term by the [miniscope-io](https://github.com/Aharoni-Lab/miniscope-io)
SDK. It moves the application from Qt 5/qmake (last releases: v1.1x,
Windows-only) to Qt 6/CMake with packaged builds for Windows, Linux, and macOS
published together from CI.

### Added

**Platforms & packaging**
- **macOS support (Apple Silicon)**: full native port, bench-validated on real
  Miniscope V4 hardware ([#83](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/pull/83)–[#89](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/pull/89)).
  Miniscopes stream through a hybrid backend: frames via a native AVFoundation
  session pinned to the scope's USB identity (immune to camera-list reshuffles
  from iPhone Continuity Camera / hot-plugs), I²C control via IOKit UVC
  requests on the control pipe — no root, no driver detach. Ships as an ad-hoc
  signed `.app` in a DMG; recordings/configs live in `~/Documents/Miniscope`.
  See `BUILD_MACOS.md`.
- **Linux support**: AppImage packaging, V4L2 device scan, udev rule, and a
  libuvc capture backend for Miniscopes so the live frame counter and BNO
  head-orientation reads work despite kernel-side UVC control caching. See
  `BUILD_LINUX.md`.
- **Windows installer** (`Setup.exe`, Inno Setup) alongside the portable zip,
  both produced by CI.
- One tagged release now publishes Windows zip + installer, Linux AppImage,
  and macOS DMG together from a single CI workflow.
- Reproducible toolchain: `environment.yml` (conda-forge) pins the same
  Qt 6.11 / OpenCV 4.13 / Python 3.12 stack on all three platforms.

**Reliability & diagnostics**
- Unit-test infrastructure (QtTest/CTest) run by CI on every PR: protocol
  packing, UVC request building, DataSaver recording behavior, macOS
  enumerator policy, bundle-path seeding, GLSL shader compile checks
  ([#84](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/pull/84)).
- Recording now **fails loudly** ([#91](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/pull/91)):
  every file the recorder creates is verified, a disk-space guard refuses to
  start below 500 MB free and stops cleanly (data preserved) before the disk
  fills, and the record button resets with an error instead of pretending to
  record when nothing can be saved.
- Live stall diagnosis on macOS (`miniscope.diag` log category): when a stream
  stalls, the DAQ frame counter is polled over the control channel to report
  whether the capture session died, the scope's video link froze, or the USB
  connection dropped ([#89](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/pull/89)).
  Silence with `QT_LOGGING_RULES="miniscope.diag=false"`.
- Multi-scope safety: with more than one Miniscope attached, the control
  channel refuses to guess which device to drive when the configured ID can't
  be resolved ([#88](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/pull/88)).

**Recorded-data quality**
- Miniscope `timeStamps.csv` files gain a **`DAQ Frame Number`** column: the
  DAQ hardware's own frame counter logged per saved frame. A jump in it is
  on-disk evidence of frames lost between the DAQ hardware and the software —
  previously undetectable — and lets recordings be aligned to the DAQ's
  per-frame TTL output post-hoc. Behavior-camera CSVs are unchanged.
- Frame timestamps now come from a **monotonic clock** instead of the wall
  clock, so an NTP sync, DST change, or manual clock adjustment mid-recording
  can no longer corrupt inter-frame intervals. CSV times are still
  milliseconds since recording start; absolute wall-clock start time remains
  in `metaData.json`.
- Stopping a recording now **drains the ring buffer** first, so the last
  frames of a session are saved instead of silently dropped.

### Changed
- Build system: qmake → **CMake** (Qt 6.4+, C++17). The old `.pro` file is no
  longer used.
- Qt 5 → **Qt 6.11**; OpenCV updated to 4.13.
- The embedded-Python DeepLabCut-Live behavior tracker is now opt-in at build
  time (`-DUSE_PYTHON=ON`); packaged Linux/macOS builds ship without it.
- `Scan Devices` is implemented per-OS (DirectShow / V4L2 / AVFoundation) and
  reports Miniscope devices explicitly.
- Closing the main window now shuts down the whole application instead of
  leaving orphaned device windows.
- Device-type dropdowns in the config editor are filtered by category; trace
  display window only opens when configured.

### Fixed
- Quitting the app now stops and joins every worker thread (capture loops,
  data saver, behavior tracker) in order — previously no thread was ever
  joined, so exit raced still-running threads against object teardown.
- The data-saver loop no longer busy-spins a full CPU core for the lifetime
  of the app; it sleeps when idle and has a proper exit path.
- Each record/stop cycle leaked every file handle and video writer it
  created; they are now released when the recording stops.
- Head-orientation CSV was silently never written when `filterBadData` was
  `false` (enable flag overwritten by a copy-paste bug) ([#90](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/pull/90)).
- Frames could be corrupted while the ring buffer was full: all capture
  backends wrote into the oldest unconsumed slot *before* reserving it — the
  slot the recorder might be encoding at that moment ([#90](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/pull/90)).
- `"Date"`/`"Time"` in `directoryStructure` (any capitalization) now produce
  date/time folders instead of literal `DateMissing` folders ([#90](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/pull/90)).
- Recording a device that never set an ROI crashed at record start (null
  dereference) ([#91](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/pull/91)).
- Video-file playback leaked its capture handle at end of playback ([#90](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/pull/90)).
- ΔF/F display crashed (uncaught OpenCV exception) when the incoming frame
  size changed mid-stream, e.g. after a camera hot-plug; it now resets the
  baseline and falls back to the raw display ([#89](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/pull/89)).
- Trace display: colormap traces no longer all turn red when a trace is
  selected; stale mouse handlers removed.
- macOS: leaked an IOKit service handle when a device exposed multiple
  VideoControl interfaces ([#85](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/pull/85)).

## [1.11] and earlier

Qt 5 / Windows-only releases. See the
[GitHub releases page](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/releases)
and the [project wiki](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/wiki).
