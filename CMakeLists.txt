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
#   -DUSE_PYTHON=ON|OFF        (default ON)
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.21)

project(MiniscopeDAQ VERSION 2.0 LANGUAGES CXX)

option(USE_PYTHON "Build the DeepLabCut-Live behaviour tracker (embedded Python)" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)   # compiles source/qml.qrc automatically

# --- Qt6 -------------------------------------------------------------------
# Note the OpenGL component: in Qt6, QOpenGLShaderProgram / QOpenGLBuffer /
# QOpenGLFramebufferObject moved out of QtGui into the separate Qt6::OpenGL
# module. The custom renderers (videodisplay, tracedisplay, behaviortracker)
# need it.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Widgets OpenGL)
qt_standard_project_setup(REQUIRES 6.5)

# This project has no .ui files (the UI is all QML), so AUTOUIC is unnecessary.
# qt_standard_project_setup() turns it on, so turn it back off here.
set(CMAKE_AUTOUIC OFF)

# --- OpenCV ----------------------------------------------------------------
find_package(OpenCV REQUIRED)

# --- Sources ---------------------------------------------------------------
set(SOURCES
    source/backend.cpp
    source/behaviorcam.cpp
    source/behaviortracker.cpp
    source/behaviortrackerworker.cpp
    source/controlpanel.cpp
    source/datasaver.cpp
    source/main.cpp
    source/miniscope.cpp
    source/newquickview.cpp
    source/tracedisplay.cpp
    source/videodevice.cpp
    source/videodisplay.cpp
    source/videostreamocv.cpp
)

set(HEADERS
    source/backend.h
    source/behaviorcam.h
    source/behaviortracker.h
    source/behaviortrackerworker.h
    source/controlpanel.h
    source/datasaver.h
    source/miniscope.h
    source/newquickview.h
    source/tracedisplay.h
    source/videodevice.h
    source/videodisplay.h
    source/videostreamocv.h
)

qt_add_executable(MiniscopeDAQ
    ${SOURCES}
    ${HEADERS}
    source/qml.qrc
)

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

    # DirectShow video-device enumeration (backEnd::scanVideoDevices): strmiids
    # provides the CLSID/IID symbols, ole32/oleaut32 the COM + VARIANT helpers.
    target_link_libraries(MiniscopeDAQ PRIVATE strmiids ole32 oleaut32)
else()
    # No bin/ split off-Windows (no launcher/deploy target there yet): the configs
    # sit next to the exe, exactly as before.
    set(MINISCOPE_RELEASE_TOP "$<TARGET_FILE_DIR:MiniscopeDAQ>")
endif()

target_include_directories(MiniscopeDAQ PRIVATE
    source
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(MiniscopeDAQ PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::OpenGL
    ${OpenCV_LIBS}
)

# --- Embedded Python (DeepLabCut-Live) -------------------------------------
if(USE_PYTHON)
    # Use a maintained interpreter (3.11/3.12). Python 3.7 used by the old
    # build is end-of-life. Override discovery with -DPython3_ROOT_DIR=...
    find_package(Python3 REQUIRED COMPONENTS Development NumPy)
    target_compile_definitions(MiniscopeDAQ PRIVATE USE_PYTHON)
    target_link_libraries(MiniscopeDAQ PRIVATE Python3::Python Python3::NumPy)
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
