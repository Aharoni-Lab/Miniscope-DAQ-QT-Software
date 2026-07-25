# ---------------------------------------------------------------------------
# Miniscope-DAQ-QT-Software  -  Qt6 / CMake build
#
# This replaces the old qmake .pro file. It is written to be *reproducible*:
# nothing is hard-coded to a particular machine. Point CMake at your toolchain
# with the standard variables, e.g.:
#
#   cmake -B build -S . ^
#     -DCMAKE_PREFIX_PATH="C:/Qt/6.5.3/msvc2019_64;C:/opencv/build" ^
#     -DPython3_ROOT_DIR="C:/Python311"
#   cmake --build build --config Release
#
# Toggle the DeepLabCut-Live behaviour tracker (embedded Python + numpy) with
#   -DUSE_PYTHON=ON|OFF        (default OFF)
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.21)

project(MiniscopeDAQ VERSION 2.0.0 LANGUAGES CXX)

# OFF by default: the embedded-Python DLC-Live tracker is used by very few
# labs, packaged builds all ship without it, and it drags Python+numpy into
# the build. Opt in with -DUSE_PYTHON=ON.
option(USE_PYTHON "Build the DeepLabCut-Live behaviour tracker (embedded Python)" OFF)

# Warnings on for OUR code (vendored thirdparty/ headers are included as
# SYSTEM, so they stay quiet). Deliberately not -Werror: a new compiler
# version must not break lab builds.
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)   # compiles source/qml.qrc automatically

# --- Qt6 -------------------------------------------------------------------
# Note the OpenGL component: in Qt6, QOpenGLShaderProgram / QOpenGLBuffer /
# QOpenGLFramebufferObject moved out of QtGui into the separate Qt6::OpenGL
# module. The custom renderers (videodisplay, tracedisplay, behaviortracker)
# need it.
# SerialPort (Qt6::SerialPort) drives the optional Open Ephys commutator. It is a
# separate conda-forge package, qt6-serialport, version-locked to qt6-main.
# The 6.8 floor is required by the ui-v3 frontend: WindowContainer (pane
# pop-out/embed) and QStyleHints::setColorScheme (dark/light theme switching).
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Widgets OpenGL SerialPort)
qt_standard_project_setup(REQUIRES 6.8)

# This project has no .ui files (the UI is all QML), so AUTOUIC is unnecessary.
# qt_standard_project_setup() turns it on, so turn it back off here.
set(CMAKE_AUTOUIC OFF)

# --- OpenCV ----------------------------------------------------------------
find_package(OpenCV REQUIRED)

# --- Shared protocol helpers -------------------------------------------------
# The Miniscope's I2C-over-UVC byte packing and BNO quaternion unpacking, in a
# small static lib so the unit tests (tests/) exercise the exact code the app
# ships. Every capture backend links this instead of duplicating the packing.
add_library(miniscope_protocol STATIC
    source/miniscopeprotocol.cpp
    source/miniscopeprotocol.h
    source/uvcrequest.h      # header-only: generic UVC 1.1 setup-packet builders
)
target_link_libraries(miniscope_protocol PUBLIC Qt6::Core)
target_include_directories(miniscope_protocol PUBLIC source)

# --- macOS UVC control transport (IOKit pipe-0) -------------------------------
# Sends UVC SET_CUR/GET_CUR to the Miniscope over the default control pipe
# without opening the interface, coexisting with Apple's UVC driver (which owns
# the streaming path since macOS 12 - libusb/libuvc would need root there).
# Consumed by the hybrid capture backend (videostreammac) and by
# uvc-bench-mac, the hardware-validation CLI: see its header comment for the
# bench procedure and pass criteria.
if(APPLE)
    enable_language(OBJCXX)   # for the AVFoundation enumerator (.mm)

    add_library(uvccontrol_mac STATIC
        source/uvccontrolmac.cpp
        source/uvccontrolmac.h
    )
    target_link_libraries(uvccontrol_mac
        PUBLIC Qt6::Core miniscope_protocol
        PRIVATE "-framework IOKit" "-framework CoreFoundation")

    # AVFoundation camera list in CAP_AVFOUNDATION deviceID order; feeds both
    # Scan Devices and the hybrid backend's index->locationID mapping.
    add_library(avfenumerator_mac STATIC
        source/avfenumeratormac.mm
        source/avfenumeratormac.h
    )
    target_link_libraries(avfenumerator_mac
        PUBLIC Qt6::Core
        PRIVATE "-framework AVFoundation" "-framework CoreMedia")
    target_include_directories(avfenumerator_mac PUBLIC source)

    # Native AVFoundation frame grabber pinned to a device uniqueID. Exists
    # because OpenCV's AVFoundation backend can only open cameras by LIST
    # INDEX, and macOS reshuffles that list as devices come and go (iPhone
    # Continuity Camera, hot-plugged webcams) - bench-verified to stream the
    # WRONG camera. ARC for safe ObjC object lifetimes.
    add_library(avfgrabber_mac STATIC
        source/avfframegrabbermac.mm
        source/avfframegrabbermac.h
    )
    set_source_files_properties(source/avfframegrabbermac.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
    target_include_directories(avfgrabber_mac PUBLIC source ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(avfgrabber_mac
        PUBLIC Qt6::Core ${OpenCV_LIBS}
        PRIVATE "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo")

    add_executable(uvc-bench-mac tools/uvc-bench-mac.cpp)
    target_link_libraries(uvc-bench-mac PRIVATE uvccontrol_mac Qt6::Core)
endif()

# --- Sources ---------------------------------------------------------------
set(SOURCES
    source/backend.cpp
    source/commutator.cpp
    source/configvalidator.cpp
    source/behaviorcam.cpp
    source/behaviortracker.cpp
    source/behaviortrackerworker.cpp
    source/controlpanel.cpp
    source/datasaver.cpp
    source/miniscope.cpp
    source/newquickview.cpp
    source/themecontroller.cpp
    source/tracedisplay.cpp
    source/twistcalculator.cpp
    source/videodevice.cpp
    source/videodisplay.cpp
    source/videostreambase.cpp
    source/videostreamocv.cpp
    source/videostreamlibuvc.cpp
)

set(HEADERS
    source/backend.h
    source/commutator.h
    source/configvalidator.h
    source/behaviorcam.h
    source/behaviortracker.h
    source/behaviortrackerworker.h
    source/controlpanel.h
    source/datasaver.h
    source/miniscope.h
    source/monotonicclock.h
    source/newquickview.h
    source/themecontroller.h
    source/tracedisplay.h
    source/twistcalculator.h
    source/videodevice.h
    source/videodisplay.h
    source/videostreamocv.h
    source/videostreambase.h
    source/videostreamlibuvc.h
)

if(APPLE)
    # The hybrid Miniscope backend (AVFoundation frames + IOKit pipe-0 controls),
    # plus the packaged-runtime path setup (bundlepaths.cpp is portable Qt so the
    # tests can run it everywhere; macOS main() calls prepareBundleRuntime()).
    list(APPEND SOURCES source/videostreammac.cpp source/bundlepaths.cpp)
    list(APPEND HEADERS source/videostreammac.h source/bundlepaths.h)
elseif(WIN32)
    # Windows main() calls BundlePaths::seedUserDataDirs() to keep user configs
    # and recordings in ~/Documents/Miniscope, outside the install dir.
    list(APPEND SOURCES source/bundlepaths.cpp)
    list(APPEND HEADERS source/bundlepaths.h)
endif()

# --- Application core library ------------------------------------------------
# Everything except main.cpp, so the session-lifecycle integration test
# (tests/tst_sessionlifecycle) can drive the real backend/session machinery.
# The QML (qml.qrc) is added to each executable, not here: resources embedded
# in a static library need Q_INIT_RESOURCE gymnastics in every consumer.
qt_add_library(miniscope_app STATIC
    ${SOURCES}
    ${HEADERS}
)
target_include_directories(miniscope_app PUBLIC
    source
    ${OpenCV_INCLUDE_DIRS}
)
# Header-only JSON Schema validator (see thirdparty/valijson/README.md).
# SYSTEM: vendored headers are not ours to fix; keep their warnings out of
# our -W flags.
target_include_directories(miniscope_app SYSTEM PUBLIC thirdparty/valijson/include)
target_link_libraries(miniscope_app PUBLIC
    miniscope_protocol
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::SerialPort
    ${OpenCV_LIBS}
)
if(APPLE)
    target_link_libraries(miniscope_app PUBLIC uvccontrol_mac avfenumerator_mac avfgrabber_mac)
endif()
if(WIN32)
    # DirectShow video-device enumeration (backEnd::scanVideoDevices): strmiids
    # provides the CLSID/IID symbols, ole32/oleaut32 the COM + VARIANT helpers.
    target_link_libraries(miniscope_app PUBLIC strmiids ole32 oleaut32)
endif()

qt_add_executable(MiniscopeDAQ
    source/main.cpp
    source/qml.qrc
)
target_link_libraries(MiniscopeDAQ PRIVATE miniscope_app)

# Single source of truth for the app version: the project() line above. The
# UI (main.cpp) and the packaging scripts all derive from it.
target_compile_definitions(MiniscopeDAQ PRIVATE MINISCOPE_VERSION="${PROJECT_VERSION}")

if(APPLE)

    # Build as a .app bundle. This is not just packaging polish: macOS kills
    # any camera-touching process whose bundle lacks NSCameraUsageDescription,
    # and TCC attributes the camera permission to the bundle identifier - a
    # bare executable gets unreliable, terminal-attributed prompts.
    set(MACOSX_BUNDLE_BUNDLE_NAME "Miniscope DAQ")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "org.aharonilab.MiniscopeDAQ")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_COPYRIGHT "Aharoni Lab, UCLA")
    set(MACOSX_BUNDLE_ICON_FILE "MiniscopeDAQ.icns")
    set_target_properties(MiniscopeDAQ PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in")
    set(MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/MiniscopeDAQ.icns")
    target_sources(MiniscopeDAQ PRIVATE "${MACOS_ICON}")
    set_source_files_properties("${MACOS_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
endif()

# conda-forge's qmlimportscanner can fail to launch (exit 0xc0000139) during
# the automatic QML import scan that Qt runs at target finalization. That scan
# only produces QML *plugin deployment* hints and is not needed to build or to
# run from the conda env, so skip it.
set_target_properties(MiniscopeDAQ PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN ON)

# Windows: GUI app (no console) + application icon, replacing qmake RC_ICONS.
if(WIN32)
    set_target_properties(MiniscopeDAQ PROPERTIES WIN32_EXECUTABLE ON)

    # Make build/<config>/ itself the portable release. The real exe (and, after
    # the `deploy` target runs, all its DLLs / Qt plugins / QML) lives in a bin/
    # subfolder, leaving the top of build/<config>/ for just the launcher and the
    # configs. So the folder a user opens IS the double-clickable release - no
    # nested dist/, no loose dev exe that fails to launch. The genexp keeps the
    # multi-config generator from appending a second per-config subdir (so the exe
    # lands in build/Release/bin, not build/Release/bin/Release). See
    # tools/launcher.cpp + tools/deploy.py.
    set(MINISCOPE_RELEASE_TOP "${CMAKE_BINARY_DIR}/$<CONFIG>")
    set_target_properties(MiniscopeDAQ PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${MINISCOPE_RELEASE_TOP}/bin")
    set(APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/appicon.rc")
    file(WRITE "${APP_ICON_RC}"
         "IDI_ICON1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/source/miniscope_icon.ico\"\n")
    target_sources(MiniscopeDAQ PRIVATE "${APP_ICON_RC}")

    # Tiny launcher that becomes dist/MiniscopeDAQ.exe: it keeps the top of the
    # distribution to a single loose file (the executable) and runs
    # bin/MiniscopeDAQ.exe with the working dir set to the top, so the DLL clutter
    # hides in bin/ while the configs stay visible at the top (see
    # tools/launcher.cpp + tools/deploy.py). Built into a side folder, NOT
    # build/Release, so the build output dir stays uncluttered; deploy.py picks it
    # up via its CMake target-file path.
    add_executable(MiniscopeLauncher WIN32 tools/launcher.cpp "${APP_ICON_RC}")
    set_target_properties(MiniscopeLauncher PROPERTIES
        OUTPUT_NAME "MiniscopeDAQ-launcher"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/launcher")

elseif(APPLE)
    # The configs ship inside the bundle at Contents/Resources; at launch,
    # BundlePaths::prepareBundleRuntime() (main.cpp) copies the app-internal
    # ones to a writable working dir and seeds the user configs to ~/Documents.
    set(MINISCOPE_RELEASE_TOP "$<TARGET_BUNDLE_CONTENT_DIR:MiniscopeDAQ>/Resources")
else()
    # No bin/ split on Linux (the AppImage wrapper handles paths there): the
    # configs sit next to the exe, exactly as before.
    set(MINISCOPE_RELEASE_TOP "$<TARGET_FILE_DIR:MiniscopeDAQ>")
endif()

# --- libuvc backend (Linux Miniscope capture) ------------------------------
# On Linux the kernel uvcvideo driver caches UVC control reads, so OpenCV/V4L2
# cannot read the Miniscope's live frame-counter / BNO head-orientation
# registers (which the firmware streams back through UVC GET_CUR). libuvc talks
# to the device directly over libusb and issues a fresh GET_CUR each read,
# bypassing that cache. Required on Linux by default; pass -DREQUIRE_LIBUVC=OFF
# to fall back to the OpenCV backend instead. Used only for Miniscopes on Linux
# (see VideoStreamLibUVC); everything else stays on OpenCV.
option(REQUIRE_LIBUVC "Fail configure on Linux if libuvc is missing (vs. OpenCV fallback)" ON)
if(UNIX AND NOT APPLE)
    find_path(LIBUVC_INCLUDE_DIR libuvc/libuvc.h)
    find_library(LIBUVC_LIBRARY NAMES uvc)
    find_library(LIBUSB1_LIBRARY NAMES usb-1.0)
    if(LIBUVC_INCLUDE_DIR AND LIBUVC_LIBRARY)
        message(STATUS "libuvc found (${LIBUVC_LIBRARY}); Miniscope Linux capture backend enabled")
        target_include_directories(miniscope_app PUBLIC ${LIBUVC_INCLUDE_DIR})
        target_link_libraries(miniscope_app PUBLIC ${LIBUVC_LIBRARY})
        if(LIBUSB1_LIBRARY)
            target_link_libraries(miniscope_app PUBLIC ${LIBUSB1_LIBRARY})
        endif()
        target_compile_definitions(miniscope_app PUBLIC HAVE_LIBUVC)
    elseif(REQUIRE_LIBUVC)
        message(FATAL_ERROR
            "libuvc not found. It is required for the Miniscope capture backend on Linux "
            "(the kernel uvcvideo driver can't return the live BNO / frame counter). "
            "Install it with `conda install -c conda-forge libuvc` or your distro's "
            "libuvc-dev, or pass -DREQUIRE_LIBUVC=OFF to build with the OpenCV fallback "
            "(no live BNO / frame counter).")
    else()
        message(WARNING "libuvc not found; Miniscopes on Linux will fall back to the "
                        "OpenCV backend (no live BNO / frame counter).")
    endif()
endif()

# --- Embedded Python (DeepLabCut-Live) -------------------------------------
if(USE_PYTHON)
    # Use a maintained interpreter (3.11/3.12). Python 3.7 used by the old
    # build is end-of-life. Override discovery with -DPython3_ROOT_DIR=...
    find_package(Python3 REQUIRED COMPONENTS Development NumPy)
    # PUBLIC: USE_PYTHON changes class layouts in headers (behaviortracker.h),
    # so every consumer of the app library must see the same definition.
    target_compile_definitions(miniscope_app PUBLIC USE_PYTHON)
    target_link_libraries(miniscope_app PUBLIC Python3::Python Python3::NumPy)
endif()

# Note: there used to be a second "MiniscopeDAQ-launch.bat" launcher here that
# just put the conda env on PATH and started the dev exe. It was superseded by
# the standalone `deploy` target below (which bundles every dependency, so the
# result needs no conda env at all) and only cluttered build/Release with an
# extra artifact that confused users, so it was removed. To run the dev build
# in place, launch from an activated conda env.

# --- Standalone deployment target ------------------------------------------
# Turn build/<config>/ itself into a clean, standalone release that runs by
# double-click with NO conda env on PATH and can be copied to any Windows PC. The
# configs are already at the top (POST_BUILD step above) and the real exe is
# already built into bin/, so deploy just bundles the DLLs into bin/ and drops the
# launcher on top (see deploy.py):
#     build/<config>/MiniscopeDAQ.exe                  <- launcher (the only loose file)
#     build/<config>/deviceConfigs|userConfigs|Scripts <- configs, visible at the top
#     build/<config>/bin/                              <- real exe + all DLLs + plugins + qml
# Run after building:
#     cmake --build build --config Release --target deploy
# Requires: run from the activated conda env, and `pip install pefile`.
# IMPORTANT: the env must use the OpenBLAS BLAS variant, not MKL (MKL is 565 MB
# of dynamically-loaded kernels that can't be bundled). Switch once with:
#     conda install "libblas=*=*openblas"
if(WIN32 AND DEFINED ENV{CONDA_PREFIX})
    add_custom_target(deploy
        COMMAND python "${CMAKE_CURRENT_SOURCE_DIR}/tools/deploy.py"
                "$<TARGET_FILE:MiniscopeDAQ>" "$ENV{CONDA_PREFIX}"
                "$<TARGET_FILE:MiniscopeLauncher>"
        DEPENDS MiniscopeDAQ MiniscopeLauncher
        USES_TERMINAL
        VERBATIM
        COMMENT "Bundling dependencies into build/<config>/ (launcher + configs at top, real exe + DLLs in bin/)")
endif()

# --- Unit tests --------------------------------------------------------------
# include(CTest) defines the standard BUILD_TESTING option (default ON) and
# calls enable_testing(). Run with:  ctest --test-dir build --output-on-failure
# (headless boxes: QT_QPA_PLATFORM=offscreen). Disable with -DBUILD_TESTING=OFF.
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# --- Runtime data ----------------------------------------------------------
# Copy the JSON device/user configs and helper scripts to the top of the release
# folder - a SINGLE copy, no duplication - where the app finds ./deviceConfigs etc.
# via its working directory. On Windows that top is build/<config>/ (the exe itself
# lives in build/<config>/bin/); elsewhere it is the exe's own directory.
foreach(data_dir deviceConfigs userConfigs Scripts)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${data_dir}")
        add_custom_command(TARGET MiniscopeDAQ POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/${data_dir}"
                "${MINISCOPE_RELEASE_TOP}/${data_dir}"
            COMMENT "Copying ${data_dir} to the release top")
    endif()
endforeach()

# Vendored third-party licenses must accompany binary distributions (e.g.
# valijson's BSD-2 notice condition). Ship each thirdparty/<name>/LICENSE as
# licenses/<name>-LICENSE.txt at the release top.
file(GLOB THIRDPARTY_LICENSES "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/*/LICENSE")
foreach(license_file ${THIRDPARTY_LICENSES})
    get_filename_component(license_dir "${license_file}" DIRECTORY)
    get_filename_component(license_name "${license_dir}" NAME)
    add_custom_command(TARGET MiniscopeDAQ POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${license_file}"
            "${MINISCOPE_RELEASE_TOP}/licenses/${license_name}-LICENSE.txt"
        COMMENT "Copying ${license_name} license to the release top")
endforeach()
