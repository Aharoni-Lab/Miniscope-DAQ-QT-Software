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

# --- DataSaver config handling (head-orientation flags, directory tokens) ------
qt_add_executable(tst_datasaver tst_datasaver.cpp
    ${CMAKE_SOURCE_DIR}/source/datasaver.cpp)
target_include_directories(tst_datasaver PRIVATE ${CMAKE_SOURCE_DIR}/source
    ${OpenCV_INCLUDE_DIRS})
target_link_libraries(tst_datasaver PRIVATE Qt6::Core Qt6::Test ${OpenCV_LIBS})
add_test(NAME datasaver COMMAND tst_datasaver)

# --- User-config schema validation + key migration ------------------------------
# Also pins the shipped schema and every shipped example config to each other:
# an example that stops validating (or a schema edit that breaks one) fails CI.
qt_add_executable(tst_configvalidator tst_configvalidator.cpp
    ${CMAKE_SOURCE_DIR}/source/configvalidator.cpp)
target_include_directories(tst_configvalidator PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_include_directories(tst_configvalidator SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/thirdparty/valijson/include)
target_compile_definitions(tst_configvalidator PRIVATE
    REPO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(tst_configvalidator PRIVATE Qt6::Core Qt6::Test)
add_test(NAME configvalidator COMMAND tst_configvalidator)

# --- Shared capture-backend machinery -------------------------------------------
# commitFrame (slot reservation, control reads, DAQ counter), the frame-size
# lock, and the reconnect backoff - via a stub backend with scripted reads.
qt_add_executable(tst_videostreambase tst_videostreambase.cpp
    ${CMAKE_SOURCE_DIR}/source/videostreambase.cpp)
target_include_directories(tst_videostreambase PRIVATE ${CMAKE_SOURCE_DIR}/source
    ${OpenCV_INCLUDE_DIRS})
target_link_libraries(tst_videostreambase PRIVATE miniscope_protocol Qt6::Core Qt6::Test ${OpenCV_LIBS})
add_test(NAME videostreambase COMMAND tst_videostreambase)

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

# --- Commutator twist math (pure quaternion logic, runs anywhere) --------------
# Pins the C++ port of Open Ephys's QuaternionToTwist: the sign convention and
# the quaternion-multiplication order (which must match, or the commutator winds
# the tether up instead of unwinding it).
qt_add_executable(tst_twistcalculator tst_twistcalculator.cpp
    ${CMAKE_SOURCE_DIR}/source/twistcalculator.cpp)
target_include_directories(tst_twistcalculator PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_link_libraries(tst_twistcalculator PRIVATE Qt6::Gui Qt6::Test)
add_test(NAME twistcalculator COMMAND tst_twistcalculator)

# --- Commutator wire format (LF framing + turn serialization) -------------------
# The host cannot see whether the controller accepted a command, so the protocol
# rules (one LF-terminated command per line; fixed, locale-independent notation)
# are pinned here instead of at the rig.
qt_add_executable(tst_commutatorprotocol tst_commutatorprotocol.cpp
    ${CMAKE_SOURCE_DIR}/source/commutator.cpp
    ${CMAKE_SOURCE_DIR}/source/twistcalculator.cpp)
target_include_directories(tst_commutatorprotocol PRIVATE ${CMAKE_SOURCE_DIR}/source)
target_link_libraries(tst_commutatorprotocol PRIVATE
    Qt6::Gui Qt6::SerialPort Qt6::Test)
add_test(NAME commutatorprotocol COMMAND tst_commutatorprotocol)

# --- Session lifecycle (Run -> endSession -> Run, one process) -----------------
# Links the full app core (miniscope_app) and drives the real backend with a
# file-playback video stream: capture thread, device window, control panel, and
# DataSaver thread all start and stop twice. Pins the re-entrancy invariant the
# ui-v3 Setup<->Acquire shell depends on. Offscreen platform: windows are
# created but never exposed, so no GL context is needed.
qt_add_executable(tst_sessionlifecycle tst_sessionlifecycle.cpp
    ${CMAKE_SOURCE_DIR}/source/qml.qrc)
target_link_libraries(tst_sessionlifecycle PRIVATE miniscope_app Qt6::Test)
# Same skip as the app target: conda-forge's qmlimportscanner can fail to launch.
set_target_properties(tst_sessionlifecycle PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN ON)
add_test(NAME sessionlifecycle COMMAND tst_sessionlifecycle)
# The backend resolves ./deviceConfigs/*.json relative to the CWD.
set_tests_properties(sessionlifecycle PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# --- Form config editor (backend form API + ConfigForm.qml) --------------------
# Round-trips a shipped example config through the form-editing API and pins
# that COMMENT_* / unknown keys survive to disk (the old tree editor dropped
# them), that QML-number doubles store as JSON ints, and that ConfigForm.qml
# instantiates against a real config and edits it.
qt_add_executable(tst_configform tst_configform.cpp
    ${CMAKE_SOURCE_DIR}/source/qml.qrc)
target_link_libraries(tst_configform PRIVATE miniscope_app Qt6::Test)
set_target_properties(tst_configform PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN ON)
add_test(NAME configform COMMAND tst_configform)
set_tests_properties(configform PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

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
