cmake_minimum_required(VERSION 3.22)
# must come before project(): the oldest macOS the plug-in runs on, and a universal binary
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
project(DrumArranger VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DRUMS_BUILD_PLUGIN "Build the JUCE plug-in (AU/VST3/Standalone)" ON)
set(DRUMS_JUCE_PATH "" CACHE PATH "Local JUCE checkout; empty = download JUCE 8.0.4")

# ---- engine: plain C++17, no JUCE --------------------------------------------
add_library(drums_core STATIC core/Engine.cpp core/Generate.cpp core/Fills.cpp core/Poly.cpp core/Midi.cpp)
target_include_directories(drums_core PUBLIC core)
set_target_properties(drums_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# identical results on every platform
if(MSVC)
    target_compile_options(drums_core PRIVATE /fp:precise)
else()
    target_compile_options(drums_core PRIVATE -ffp-contract=off -Wall -Wextra)
endif()

add_executable(drums_cli cli/drums_main.cpp)
target_link_libraries(drums_cli PRIVATE drums_core)
add_executable(drums_tests tests/core_test.cpp)
target_link_libraries(drums_tests PRIVATE drums_core)

# ---- plug-in ----------------------------------------------------------------
if(DRUMS_BUILD_PLUGIN)
    if(DRUMS_JUCE_PATH)
        add_subdirectory(${DRUMS_JUCE_PATH} juce)
    else()
        include(FetchContent)
        FetchContent_Declare(juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW ON)
        FetchContent_MakeAvailable(juce)
    endif()

    juce_add_plugin(DrumArranger
        COMPANY_NAME "Teilormusic"
        BUNDLE_ID "com.teilormusic.drumarranger"
        PLUGIN_MANUFACTURER_CODE Tlrm
        PLUGIN_CODE Drum
        FORMATS AU VST3 Standalone
        PRODUCT_NAME "Drum Arranger"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        VST3_CATEGORIES Instrument Tools)
    target_sources(DrumArranger PRIVATE
        plugin/PluginProcessor.cpp plugin/PluginEditor.cpp plugin/GridView.cpp plugin/Look.cpp)
    target_compile_definitions(DrumArranger PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(DrumArranger
        PRIVATE drums_core juce::juce_audio_utils juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
               juce::juce_recommended_warning_flags)

    # Drum Arranger Live: the MIDI FX (Logic: MIDI FX slot -> Audio Units -> Teilormusic)
    juce_add_plugin(DrumArrangerLive
        COMPANY_NAME "Teilormusic"
        BUNDLE_ID "com.teilormusic.drumarrangerlive"
        PLUGIN_MANUFACTURER_CODE Tlrm
        PLUGIN_CODE DrLv
        FORMATS AU VST3
        PRODUCT_NAME "Drum Arranger Live"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
        VST3_CATEGORIES Fx Tools)
    target_sources(DrumArrangerLive PRIVATE plugin/LiveProcessor.cpp plugin/LiveEditor.cpp plugin/Look.cpp)
    target_compile_definitions(DrumArrangerLive PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(DrumArrangerLive
        PRIVATE drums_core juce::juce_audio_utils juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
               juce::juce_recommended_warning_flags)
endif()

# ---- plug-in tests: state, preview, live mode and editor snapshots (needs a display) -------
option(DRUMS_BUILD_TESTS "Build the plug-in test runner" OFF)
if(DRUMS_BUILD_PLUGIN AND DRUMS_BUILD_TESTS)
    juce_add_console_app(DrumPluginTests PRODUCT_NAME "DrumPluginTests")
    target_sources(DrumPluginTests PRIVATE tests/plugin_test.cpp
        plugin/PluginProcessor.cpp plugin/PluginEditor.cpp plugin/GridView.cpp plugin/Look.cpp
        plugin/LiveProcessor.cpp plugin/LiveEditor.cpp)
    target_include_directories(DrumPluginTests PRIVATE plugin)
    target_compile_definitions(DrumPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="Drum Arranger" JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
        DRUMS_NO_PLUGIN_ENTRY=1)
    target_link_libraries(DrumPluginTests PRIVATE drums_core juce::juce_audio_utils juce::juce_gui_extra
        juce::juce_recommended_config_flags)
endif()
