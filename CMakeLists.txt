cmake_minimum_required(VERSION 3.22)
cmake_policy(SET CMP0091 NEW)
project(Quanpressor VERSION 1.0.1)

# Ship the Windows plugin with the static MSVC runtime: /MT in Release and
# /MTd in Debug. This keeps the VST3 self-contained with its bundled deps.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(PATINA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Patina" CACHE PATH
    "Path to the Patina include directory (the directory containing patina.h)")
if(NOT EXISTS "${PATINA_DIR}/include/patina.h")
    message(FATAL_ERROR
        "Quanpressor: patina.h was not found. Set PATINA_DIR to the Patina source directory, "
        "for example -DPATINA_DIR=D:/dev/Patina")
endif()

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.6)
FetchContent_MakeAvailable(JUCE)

add_library(SoundRaveSodium STATIC IMPORTED GLOBAL)
if(APPLE)
    set_target_properties(SoundRaveSodium PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libs/libsodium/libsodium_universal.a")
elseif(WIN32)
    set_target_properties(SoundRaveSodium PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libs/libsodium/libsodium.lib")
else()
    message(FATAL_ERROR "Quanpressor: bundled libsodium is available only for macOS and Windows")
endif()

juce_add_plugin(Quanpressor
    COMPANY_NAME SoundRave
    BUNDLE_ID org.soundrave.quanpressor
    PLUGIN_MANUFACTURER_CODE SRav
    PLUGIN_CODE QnPr
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Quanpressor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    # JUCE 8.0.6's Windows manifest helper can write moduleinfo.json but
    # occasionally fails to exit afterwards. Install the equivalent static
    # manifest below so VST3 builds remain deterministic.
    VST3_AUTO_MANIFEST FALSE)

get_target_property(QUANPRESSOR_VST3_ARTEFACT Quanpressor_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/moduleinfo.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Quanpressor-moduleinfo.json"
    @ONLY)
add_custom_command(TARGET Quanpressor_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${QUANPRESSOR_VST3_ARTEFACT}/Contents/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/Quanpressor-moduleinfo.json"
        "${QUANPRESSOR_VST3_ARTEFACT}/Contents/Resources/moduleinfo.json"
    VERBATIM)

juce_generate_juce_header(Quanpressor)

target_sources(Quanpressor PRIVATE
    Source/DesktopStandalone.cpp Source/ProductStorage.h
    Source/PluginProcessor.cpp Source/PluginProcessor.h
    Source/PluginEditor.cpp Source/PluginEditor.h Source/QuanpressorInfoOverlay.h
    Source/MeterPrivacyRenderer.cpp Source/MeterPrivacyRenderer.h
    Source/QuanpressorDSP.h Source/QuanpressorFactoryPresets.h Source/Metering.h
    Source/license/LicenseManager.cpp Source/license/LicenseManager.h
    Source/license/LicenseComponent.h Source/license/AudioRestrictor.h
    JuceLibraryCode/BinaryData.cpp JuceLibraryCode/BinaryData2.cpp
    JuceLibraryCode/BinaryData.h)

target_include_directories(Quanpressor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PATINA_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/libsodium/include)

if(APPLE OR WIN32)
    target_compile_definitions(Quanpressor PUBLIC JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
endif()

target_compile_definitions(Quanpressor PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    $<$<PLATFORM_ID:Windows>:SODIUM_STATIC>)

target_link_libraries(Quanpressor PRIVATE
    SoundRaveSodium
    juce::juce_audio_utils juce::juce_data_structures juce::juce_dsp juce::juce_opengl
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(QUANPRESSOR_BUILD_STORAGE_TESTS "Build product storage migration tests" OFF)
if(QUANPRESSOR_BUILD_STORAGE_TESTS)
    enable_testing()
    juce_add_console_app(QuanpressorStorageTests PRODUCT_NAME "Quanpressor Storage Tests")
    juce_generate_juce_header(QuanpressorStorageTests)
    target_sources(QuanpressorStorageTests PRIVATE Tests/StorageMigrationTest.cpp Source/ProductStorage.h)
    target_link_libraries(QuanpressorStorageTests PRIVATE juce::juce_core)
    target_compile_definitions(QuanpressorStorageTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    add_test(NAME QuanpressorStorageTests COMMAND QuanpressorStorageTests)
endif()
