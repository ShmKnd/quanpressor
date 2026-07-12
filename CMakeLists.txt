cmake_minimum_required(VERSION 3.22)
project(Quanpressor VERSION 0.1.0)

include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.6)
FetchContent_MakeAvailable(JUCE)

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
    Source/PluginEditor.cpp Source/PluginEditor.h
    Source/QuanpressorDSP.h Source/Metering.h)

target_include_directories(Quanpressor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Patina)

target_compile_definitions(Quanpressor PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Quanpressor PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
