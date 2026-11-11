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
