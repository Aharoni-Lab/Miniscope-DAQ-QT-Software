QT += quick widgets
CONFIG += c++11


# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Refer to the documentation for the
# deprecated API to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
        backend.cpp \
        behaviorcam.cpp \
        behaviortracker.cpp \
        controlpanel.cpp \
        datasaver.cpp \
        main.cpp \
        miniscope.cpp \
        newquickview.cpp \
        videodisplay.cpp \
        videostreamocv.cpp

RESOURCES += qml.qrc

# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH =

# Additional import path used to resolve QML modules just for Qt Quick Designer
QML_DESIGNER_IMPORT_PATH =

INCLUDEPATH += C:\opencv-build\install\include


LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_core412.dll.a
LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_highgui412.dll.a
LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_imgcodecs412.dll.a
LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_imgproc412.dll.a
LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_features2d412.dll.a
LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_calib3d412.dll.a
LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_imgproc412.dll.a
LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_videoio412.dll.a

#LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_core412d.dll.a
#LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_highgui412d.dll.a
#LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_imgcodecs412d.dll.a
#LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_imgproc412d.dll.a
#LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_features2d412d.dll.a
#LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_calib3d412d.dll.a
#LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_imgproc412d.dll.a
#LIBS += C:\opencv-build\install\x64\mingw\lib\libopencv_videoio412d.dll.a


# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

HEADERS += \
    backend.h \
    behaviorcam.h \
    behaviortracker.h \
    controlpanel.h \
    datasaver.h \
    miniscope.h \
    newquickview.h \
    videodisplay.h \
    videostreamocv.h
