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

project(MiniscopeDAQ VERSION 1.10 LANGUAGES CXX)

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
    set(APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/appicon.rc")
    file(WRITE "${APP_ICON_RC}"
         "IDI_ICON1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/source/miniscope_icon.ico\"\n")
    target_sources(MiniscopeDAQ PRIVATE "${APP_ICON_RC}")
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

# --- Double-click launcher (Windows + conda) -------------------------------
# conda packages run from inside their environment, not standalone: OpenCV is
# split across ~30 module DLLs plus FFmpeg/codec/image deps, and Python is
# embedded too. Rather than chase a fragile full bundle (windeployqt only
# handles Qt), generate a launcher that puts the env on PATH and starts the
# exe, so it can be double-clicked from Explorer.
if(WIN32)
    if(DEFINED ENV{CONDA_PREFIX})
        file(TO_NATIVE_PATH "$ENV{CONDA_PREFIX}" MS_ENV_NATIVE)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/launcher.bat.in"
            "${CMAKE_CURRENT_BINARY_DIR}/MiniscopeDAQ-launch.bat"
            @ONLY)
        add_custom_command(TARGET MiniscopeDAQ POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_BINARY_DIR}/MiniscopeDAQ-launch.bat"
                "$<TARGET_FILE_DIR:MiniscopeDAQ>/MiniscopeDAQ-launch.bat"
            COMMENT "Placing double-click launcher (MiniscopeDAQ-launch.bat) next to the exe")
    else()
        message(STATUS "CONDA_PREFIX not set at configure time; "
                       "skipping double-click launcher generation. "
                       "Configure from an activated conda env to enable it.")
    endif()
endif()

# --- Runtime data ----------------------------------------------------------
# Copy the JSON device/user configs and helper scripts next to the binary,
# replicating the old qmake copydata steps.
foreach(data_dir deviceConfigs userConfigs Scripts)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${data_dir}")
        add_custom_command(TARGET MiniscopeDAQ POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/${data_dir}"
                "$<TARGET_FILE_DIR:MiniscopeDAQ>/${data_dir}"
            COMMENT "Copying ${data_dir} to build output")
    endif()
endforeach()
