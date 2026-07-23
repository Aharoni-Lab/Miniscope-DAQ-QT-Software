# Unit tests (Qt Test + CTest). Zero extra dependencies: Qt6::Test ships with
# the qt6-main package the build already uses.
find_package(Qt6 REQUIRED COMPONENTS Test)

# --- Protocol packing/unpacking (pure logic, runs anywhere) -------------------
qt_add_executable(tst_miniscopeprotocol tst_miniscopeprotocol.cpp)
target_link_libraries(tst_miniscopeprotocol PRIVATE miniscope_protocol Qt6::Test)
add_test(NAME miniscopeprotocol COMMAND tst_miniscopeprotocol)

# --- UVC control-request builders (pure logic, runs anywhere) ------------------
qt_add_executable(tst_uvcrequest tst_uvcrequest.cpp)
target_link_libraries(tst_uvcrequest PRIVATE miniscope_protocol Qt6::Test)
add_test(NAME uvcrequest COMMAND tst_uvcrequest)

# --- .app bundle runtime path setup (pure Qt file ops, runs anywhere) ----------
# Only macOS main() calls this code, but the copy/seed semantics are portable -
# so every platform's CI pins them down.
qt_add_executable(tst_bundlepaths tst_bundlepaths.cpp
    ${CMAKE_SOURCE_DIR}/source/bundlepaths.cpp)
target_include_directories(tst_bundlepaths PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_link_libraries(tst_bundlepaths PRIVATE Qt6::Core Qt6::Test)
add_test(NAME bundlepaths COMMAND tst_bundlepaths)

# --- macOS IOKit UVC transport (hardware-free smoke tests) ---------------------
if(APPLE)
    qt_add_executable(tst_uvccontrolmac tst_uvccontrolmac.cpp)
    target_link_libraries(tst_uvccontrolmac PRIVATE uvccontrol_mac Qt6::Test)
    add_test(NAME uvccontrolmac COMMAND tst_uvccontrolmac)

    # AVFoundation index -> USB identity mapping (uniqueID parse + smoke).
    qt_add_executable(tst_avfenumerator tst_avfenumerator.cpp)
    target_link_libraries(tst_avfenumerator PRIVATE avfenumerator_mac Qt6::Test Qt6::Gui)
    add_test(NAME avfenumerator COMMAND tst_avfenumerator)

    # uniqueID-pinned frame grabber: failure paths must fail cleanly.
    qt_add_executable(tst_avfframegrabber tst_avfframegrabber.cpp)
    target_link_libraries(tst_avfframegrabber PRIVATE avfgrabber_mac Qt6::Test)
    add_test(NAME avfframegrabber COMMAND tst_avfframegrabber)
endif()

# --- Shader compile check ------------------------------------------------------
# Compiles + links every custom GLSL program in an offscreen GL context, so a
# shader edit that breaks any platform's GL dialect fails CI instead of failing
# at runtime when a device window opens. Skips itself (QSKIP) where no GL >= 2.1
# context is available (e.g. Windows runners without a GPU: software GL is 1.1).
qt_add_executable(tst_shadercompile tst_shadercompile.cpp)
target_link_libraries(tst_shadercompile PRIVATE Qt6::Test Qt6::Gui Qt6::OpenGL)
target_compile_definitions(tst_shadercompile PRIVATE
    SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/source/shaders")
add_test(NAME shadercompile COMMAND tst_shadercompile)
if(UNIX AND NOT APPLE)
    # Headless Linux (CI): no display server, so use the offscreen platform;
    # Mesa's software GL still provides a real 2.1+ context there.
    set_tests_properties(shadercompile PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
