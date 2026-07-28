cmake_minimum_required(VERSION 3.22)
project(Quanpressor VERSION 0.1.0)

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
    PLUGIN_MANUFACTURER_CODE SdRv
    PLUGIN_CODE QnPr
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Quanpressor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Quanpressor PRIVATE
    Source/PluginProcessor.cpp Source/PluginProcessor.h
    Source/PluginEditor.cpp Source/PluginEditor.h Source/QuanpressorInfoOverlay.h
    Source/MeterPrivacyRenderer.cpp Source/MeterPrivacyRenderer.h
    Source/QuanpressorDSP.h Source/Metering.h
    Source/license/LicenseManager.cpp Source/license/LicenseManager.h
    Source/license/LicenseComponent.h Source/license/AudioRestrictor.h
    JuceLibraryCode/BinaryData.cpp JuceLibraryCode/BinaryData2.cpp
    JuceLibraryCode/BinaryData.h)

target_include_directories(Quanpressor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Patina
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/libsodium/include)

target_compile_definitions(Quanpressor PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    $<$<PLATFORM_ID:Windows>:SODIUM_STATIC>)

target_link_libraries(Quanpressor PRIVATE
    SoundRaveSodium
    juce::juce_audio_utils juce::juce_data_structures juce::juce_dsp juce::juce_opengl
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
