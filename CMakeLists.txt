cmake_minimum_required(VERSION 3.22)
project(WSHAPER VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

# JUCE: cartella locale se c'e' (JUCE_DIR), altrimenti scaricato da GitHub
if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory(${JUCE_DIR} juce)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(WSHAPER_FORMATS AU VST3 Standalone)
else()
    set(WSHAPER_FORMATS VST3)
endif()

juce_add_plugin(WShaper
    COMPANY_NAME "CONTROLUNA"
    BUNDLE_ID "com.controluna.wshaper"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Umsm
    PLUGIN_CODE Wshp
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx EQ
    FORMATS ${WSHAPER_FORMATS}
    PRODUCT_NAME "W-Shaper")

target_sources(WShaper PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(WShaper PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(NOT APPLE)
    target_compile_definitions(WShaper PUBLIC JUCE_ALSA=0 JUCE_JACK=0)
endif()

target_link_libraries(WShaper
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Solo per le prove su Linux: disegna l'interfaccia vera in PNG
option(WSHAPER_SNAPSHOT "Build the UI snapshot tool" OFF)
if(WSHAPER_SNAPSHOT)
    juce_add_console_app(WShaperSnap PRODUCT_NAME "WShaperSnap")
    target_sources(WShaperSnap PRIVATE tools/Snap.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(WShaperSnap PRIVATE JucePlugin_Name="W-Shaper" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 SMOKER_SNAPSHOT=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(WShaperSnap PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics
                          juce::juce_recommended_config_flags)
endif()

# Stress test con AddressSanitizer (solo Linux)
option(WSHAPER_STRESS "Build the stress test" OFF)
if(WSHAPER_STRESS)
    juce_add_console_app(WShaperStress PRODUCT_NAME "WShaperStress")
    target_sources(WShaperStress PRIVATE tools/Stress.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(WShaperStress PRIVATE JucePlugin_Name="W-Shaper" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 SMOKER_SNAPSHOT=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_compile_options(WShaperStress PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(WShaperStress PRIVATE -fsanitize=address)
    target_link_libraries(WShaperStress PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics)
endif()
