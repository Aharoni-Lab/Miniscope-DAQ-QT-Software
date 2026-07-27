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

**User interface (v3 rewrite)**
- **Single-window app**: a launcher-less shell with two modes — *Setup*
  (recent configs plus a form-based config editor) and *Acquire*. Ending a
  session returns to Setup, so switching configs no longer requires
  restarting the program.
- **Form config editor** replaces the JSON tree: cards for
  General/Recording/Devices/Trace display/Behavior tracker/Commutator with
  device-catalog-driven dropdowns (gain, frame rate, codec…), plus a raw-JSON
  tab for anything not surfaced in the form. Unknown/`COMMENT_` keys are
  preserved on save. Unsaved edits are flagged, and Run/Open/New prompt to
  save first.
- **Embedded video panes with pop-out**: device streams, and the trace
  display, dock as panes in a grid inside the main window; any pane can pop
  out to a floating window and dock back. Which panes float and their
  positions persist **per config file**, so a rig's arrangement comes back on
  every run. A Lock Layout toggle prevents accidental changes.
- **Session bar**: record/stop transport with a live clock (stop is
  hold-to-confirm), note logging while recording, external-trigger toggle,
  disk-free readout with low-space warnings, per-device FPS / dropped-frame /
  buffer chips, and a collapsible session message log — telemetry that
  previously lived in a separate control panel window or was hidden in
  hover menus.
- **Redesigned video windows**: always-visible status chips (REC indicator,
  FPS, drops, buffer) and always-visible sensor-value chips (LED, gain,
  focus…) whose slider/stepper slides out on hover — set values stay
  glanceable without eating video space. Display-only controls
  (contrast/brightness, saturation highlight, LUT colormap, ΔF/F) and
  actions sit in an auto-hiding, pinnable side rail. The recording-ROI
  selection now draws its rectangle over the video and stays outlined after
  committing.
- **Dark theme** by default with a light toggle (persisted); native dialogs
  follow the app theme.

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
  both produced by CI. On Windows, your user configs and recordings now live in
  `Documents\Miniscope` (matching macOS and Linux) instead of inside the install
  folder, so they survive upgrades and uninstalls; the installed folder holds
  only read-only example configs.
- One tagged release now publishes Windows zip + installer, Linux AppImage,
  and macOS DMG together from a single CI workflow, as a reviewable draft with
  the changelog as its release notes.
- All platforms now build, test, and ship the same configuration — the optional
  DeepLabCut-Live tracker (embedded Python) is no longer bundled on Windows,
  matching macOS and Linux. Build it locally with `-DUSE_PYTHON=ON` if needed.
- Reproducible toolchain: `environment.yml` (conda-forge) pins the same
  Qt 6.11 / OpenCV 4.13 / Python 3.12 stack (to major.minor) on all three
  platforms; `conda-lock.yml` records the exact package set per release.

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
- macOS behavior cameras are now **pinned to the physical camera** (its
  AVFoundation uniqueID), resolved once from the config's `deviceID` at
  connect. Reconnects after a drop only ever rebind the same physical camera —
  previously a reconnect reopened by list index, which could silently switch
  to a different camera (e.g. the built-in one) mid-session.

**Config files**
- User configs now have a real **JSON Schema**
  (`deviceConfigs/userConfigSchema.json`). Configs are validated against it at
  load: wrong types, out-of-range values, and malformed sections are reported
  with their exact location instead of being silently replaced by defaults.
  Validation never blocks running, and extra keys (notes, `COMMENT_*`,
  custom directoryStructure tokens) are always allowed.
- Example configs (and configs saved from the in-app editor) carry a
  `$schema` pointer, so editing them in VS Code gives live validation and
  autocomplete, plus a `configVersion` stamp for future format changes.
- The `recordLengthinSeconds` typo is corrected to `recordLengthInSeconds`;
  the old spelling keeps working forever (a note is shown when it's used).
- Fixed two documented config keys the code never actually read:
  `executableOnStartRecording.arguments` (code only read a top-level
  `arguments` key) and `behaviorTracker.occupancyPlot.numBinsX/numBinsY`
  (code read `numBinX/numBinY`, so configured bins were silently ignored).
  The documented spellings now work; the old ones remain accepted.
- A malformed config file now reports the JSON parse error and its location
  instead of silently loading as an empty config.
- Removed `deviceConfigs/miniscopes.json` and `behaviorCams.json` — dead
  since the device catalog moved to `videoDevices.json`, but they looked
  authoritative and old wiki instructions pointed users at them.
- **Opt-in fine `led0` steps** for V4 miniscopes: set
  `"led0FineSteps": true` on the device in the user config and the
  illumination slider runs 0–255, addressing the LED driver's 255 hardware
  steps individually instead of 0–100%, giving 2.55× finer steps — bright
  preps needing very dim illumination asked for this
  ([#68](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/issues/68)/[#69](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/pull/69)).
  Both mappings span the same brightness range. Without the flag, nothing
  changes: existing configs keep their exact LED output.

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

**Hardware integration**
- **Open Ephys commutator control**: the software can now drive an
  [Open Ephys commutator](https://github.com/open-ephys/commutator-controller)
  directly over its USB serial port, so the tether unwinds as the animal turns
  — no separate Bonsai workflow required. A V4 (or newer) Miniscope's live BNO
  head orientation is converted to incremental motor turns (a C++ port of the
  [bonsai-commutator](https://github.com/open-ephys/bonsai-commutator)
  `QuaternionToTwist` algorithm) and streamed to the device on its own thread.
  Enable it with a top-level `commutator` block in the user config (`enabled`,
  `port`, optional `deviceName`, and — for non-standard mounts — `headstageAxis`
  / `commutatorAxis` / `fallbackMode`). Off unless configured.

### Changed
- Build system: qmake → **CMake** (Qt 6.8+, C++17). The old `.pro` file is no
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
- Compiler warnings are on (`-Wall -Wextra` / `/W4`) and the build is
  warning-clean; removed the stale qmake `.pro` file, the tracked
  `My User Configs/` folder, and dead libusb test code.
- The three capture backends (OpenCV, libuvc, macOS hybrid) now share one
  per-frame pipeline and one reconnect policy in a common base class instead
  of three drifting copies. All backends reconnect with **backoff** (1 s
  doubling to a 5 s cap, one warning per outage instead of one per second —
  the Windows/Linux paths previously retried in a fixed 1 s spam loop).
- A camera that starts delivering a different frame size mid-stream (e.g. a
  reconnect that renegotiated the resolution) has those frames dropped with
  an error message instead of writing mixed-size frames into the recording.

### Fixed
- Windows on hybrid-GPU laptops (iGPU + discrete): resizing a session window
  stalled for seconds per step (lagging the whole desktop) because the app's
  OpenGL rendering defaulted to the power-saving iGPU, where the session's
  multiple GL windows serialize their swapchain resizes pathologically. The
  app now requests the discrete GPU via the standard NVIDIA/AMD driver
  opt-ins (bench: a 50-step scripted resize storm dropped from 22.6 s to
  3.0 s, with webcam capture holding ~30 FPS instead of starving to ~6 FPS).
- A transient camera grab/retrieve failure (e.g. GPU/driver contention while
  a window is resized) no longer releases the camera and triggers a full
  disconnect/reconnect cycle - the capture loop retries in place for up to
  ~1 s first. A genuinely unplugged camera still reconnects as before.
- Windows standalone build: opening any device window crashed the app
  (access violation in Qt6Core) because the deployed layout was missing a
  `qt.conf` — Qt's relocatable path lookup pointed at nonexistent
  directories, so the per-device QML engines could not find modules like
  `QtQuick.Window`. `deploy.py` now writes the `qt.conf`, matching what
  macdeployqt (macOS) and linuxdeploy-plugin-qt (Linux) already do for the
  other platforms' bundles. A device window whose QML fails to load for any
  other reason now logs the loader errors and disables that device instead
  of crashing.
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
- Behavior tracker: a `poseOverlay` config without a `skeleton` section had
  its pose overlay silently disabled even when `enabled` was true (assignment
  instead of comparison in the skeleton check).
- Quitting (and window creation) no longer spams QML `TypeError` messages to
  the console: every declarative `backend`/`parent` binding is null-guarded
  against the teardown/startup window where the referenced object doesn't
  exist yet or anymore.
- A failed UVC control read used to be indistinguishable from reading the
  value 0: it could fake an external-trigger edge, fabricate an all-zero
  head-orientation quaternion, and corrupt the DAQ frame-counter offset for
  the whole recording if it happened on the first frame. Failed reads are now
  skipped (trigger), hold the last good value (BNO), and log `-1` in the
  `DAQ Frame Number` CSV column so the gap is visible post-hoc.
- Linux cameras on the OpenCV backend could never reconnect after a drop
  (the V4L2 case was missing from the reconnect path, which retried forever).
- Quitting while a device was mid-reconnect could crash: the reconnect wait
  slept longer than shutdown waits for the capture thread, which could then
  wake and write into freed buffers. The wait now checks the stop flag every
  100 ms.
- Control commands issued while a device was disconnected no longer flush
  ahead of the SERDES mode packets when it reconnects (the mode must be the
  first traffic on the link; device state is re-sent right after anyway).
- Device connection errors (wrong `deviceID`, device busy, resolve failures)
  now appear in the message console — they used to be emitted before the
  message log was listening, so only a generic "cannot connect" ever showed.
- The live **"Dropped Frames"** readout no longer sticks at `N/A` for the rest
  of a session after a Miniscope is unplugged and reconnected. The device's
  frame counter restarts when it power-cycles (as the recorded `DAQ Frame
  Number` column intentionally shows), which left the live readout permanently
  negative; it is now measured within each connection span. The recorded CSV
  column is unchanged.

## [1.11] and earlier

Qt 5 / Windows-only releases. See the
[GitHub releases page](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/releases)
and the [project wiki](https://github.com/Aharoni-Lab/Miniscope-DAQ-QT-Software/wiki).
