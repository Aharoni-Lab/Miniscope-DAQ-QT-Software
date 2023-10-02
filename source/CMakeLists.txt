# Generated from Miniscope-DAQ-QT-Software.pro.

cmake_minimum_required(VERSION 3.16)
project(Miniscope-DAQ-QT-Software VERSION 1.0 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)

set(CMAKE_GENERATOR "Ninja")

if(NOT DEFINED INSTALL_EXAMPLESDIR)
    set(INSTALL_EXAMPLESDIR "examples")
endif()

set(INSTALL_EXAMPLEDIR "${INSTALL_EXAMPLESDIR}")

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS 3DCore Gui Qml Quick Widgets OpenGL)

# openCV
find_package( OpenCV REQUIRED )
include_directories( ${OpenCV_INCLUDE_DIRS} )

# Python
find_package(Python3 REQUIRED COMPONENTS Interpreter Development NumPy)
include_directories(${Python3_INCLUDE_DIRS})
include_directories(${Python3_NumPy_INCLUDE_DIRS})



qt_add_executable(Miniscope-DAQ-QT-Software WIN32 MACOSX_BUNDLE
    backend.cpp backend.h
    behaviorcam.cpp behaviorcam.h
    behaviortracker.cpp behaviortracker.h
    behaviortrackerworker.cpp behaviortrackerworker.h
    controlpanel.cpp controlpanel.h
    datasaver.cpp datasaver.h
    main.cpp
    miniscope.cpp miniscope.h
    newquickview.cpp newquickview.h
    tracedisplay.cpp tracedisplay.h
    videodevice.cpp videodevice.h
    videodisplay.cpp videodisplay.h
    videostreamocv.cpp videostreamocv.h
)
qt_add_qml_module(Miniscope-DAQ-QT-Software
    URI Miniscope-DAQ-QT-Software
    VERSION ${PROJECT_VERSION}
    QML_FILES
        main.qml
        Miniscope_V4.qml
        behaviorCam.qml
        FpsItem.qml
        VideoSliderControl.qml
        TopMenu.qml
        VideoSpinBoxControl.qml
        ControlPanel.qml
        BNODisplay.qml
        Miniscope_V4_BNO.qml
        behaviorTracker.qml
        Miniscope_V3.qml
        TraceDisplayWindow.qml
        TreeViewerJSON.qml
    RESOURCES
        img/iconEWL.png
        img/icon/ewl.ico
        img/icon/led.ico
        img/MiniscopeLogo.png
        shaders/imageBasic.vert
        shaders/imageSaturationScaling.frag
        img/icon/exposure.ico
        img/icon/gain.ico
        img/icon/alpha.ico
        img/icon/beta.ico
        img/Miniscope_Logo_BNO.png
        img/icon/alpha.png
        img/icon/beta.png
        img/icon/ewl.png
        img/icon/fps.png
        img/icon/gain.png
        img/icon/led.png
        shaders/grid.vert
        shaders/grid.frag
        shaders/movingBar.vert
        shaders/movingBar.frag
        shaders/trace.frag
        shaders/trace.vert
        shaders/texture.frag
        shaders/texture.vert
        shaders/tracker.vert
        shaders/tracker.frag
        shaders/trackerOverlay.frag
        shaders/trackerOverlay.vert
    NO_RESOURCE_TARGET_PATH
)

target_compile_definitions(Miniscope-DAQ-QT-Software PUBLIC
    QT_DEPRECATED_WARNINGS
    USE_PYTHON
)

target_link_libraries(Miniscope-DAQ-QT-Software PUBLIC
    Qt6::3DCore
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Widgets
    Qt6::OpenGL
    ${OpenCV_LIBS}
    ${Python3_LIBRARIES}
)

target_include_directories(Miniscope-DAQ-QT-Software PRIVATE
    ${Python3_INCLUDE_DIRS}
    ${Python3_NumPy_INCLUDE_DIRS}
)


if(WIN32)
    target_include_directories(Miniscope-DAQ-QT-Software PUBLIC
        C:/Users/dbaha/.conda/envs/basepy37/Lib/site-packages/numpy/core/include
        C:/Users/dbaha/.conda/envs/basepy37/include
        C:/opencv-4.4.0/build/install/include
    )

    target_link_libraries(Miniscope-DAQ-QT-Software PUBLIC
        # Remove: LC:/Users/dbaha/.conda/envs/basepy37/libs
        # Remove: LC:/opencv-4.4.0/build/lib/Release
        opencv_world440
        python37
    )
elseif(APPLE)
    set_target_properties(Miniscope-DAQ-QT-Software PROPERTIES
        MACOSX_BUNDLE TRUE
    )
elseif(UNIX)
#    target_include_directories(Miniscope-DAQ-QT-Software PUBLIC
#        /usr/local/Cellar/opencv/4.8.0_1/include/opencv4/opencv2
#        ~/.pyenv/versions/3.7.12/include/python3.7m
#        ~/.pyenv/versions/3.7.12/lib/python3.7/site-packages/numpy/core/include
#    )
endif()



install(TARGETS Miniscope-DAQ-QT-Software
    RUNTIME DESTINATION "${INSTALL_EXAMPLEDIR}"
    BUNDLE DESTINATION "${INSTALL_EXAMPLEDIR}"
    LIBRARY DESTINATION "${INSTALL_EXAMPLEDIR}"
)
