cmake_minimum_required(VERSION 3.22)
project(RingsPlugin VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Plugin formats: AU + VST3 (+ Standalone for quick testing).
set(FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND FORMATS AU)
endif()

juce_add_plugin(Rings
  COMPANY_NAME "particledots"
  PLUGIN_MANUFACTURER_CODE Ptdt
  PLUGIN_CODE Ring
  FORMATS ${FORMATS}
  PRODUCT_NAME "Unofficial Rings"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicEffect   # lets Logic route MIDI to it as a "MIDI-controlled effect"
  COPY_PLUGIN_AFTER_BUILD TRUE)

# --- DSP core: vendored Rings/stmlib code + our JUCE-free wrapper, built with plain flags ---
add_library(rings_core STATIC
  src/RingsEngine.cpp
  third_party/rings/dsp/part.cc
  third_party/rings/dsp/string_synth_part.cc
  third_party/rings/dsp/resonator.cc
  third_party/rings/dsp/string.cc
  third_party/rings/dsp/fm_voice.cc
  third_party/rings/resources.cc
  third_party/stmlib/utils/random.cc
  third_party/stmlib/dsp/units.cc)
target_include_directories(rings_core PUBLIC src third_party)
target_compile_definitions(rings_core PRIVATE TEST=1)
set_target_properties(rings_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Some upstream headers rely on <cstdio>/<cstdlib>/<cmath>/<cstdint> being pulled in transitively.
if(MSVC)
  target_compile_options(rings_core PRIVATE /FIcstdio /FIcstdlib /FIcmath /FIcstdint)
else()
  target_compile_options(rings_core PRIVATE "SHELL:-include cstdio" "SHELL:-include cstdlib" "SHELL:-include cmath" "SHELL:-include cstdint" -Wno-unused-local-typedef)
endif()

# --- The plugin ---
target_sources(Rings PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp)
target_compile_definitions(Rings PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Rings PRIVATE
  rings_core
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

# --- Headless test of the DSP wrapper:  cmake --build build --target engine_test && build/engine_test ---
add_executable(engine_test EXCLUDE_FROM_ALL src/engine_test.cpp)
target_link_libraries(engine_test PRIVATE rings_core)


# --- Model-switching regression test:  JUNK=0xFF build/sweep_test ---
add_executable(sweep_test EXCLUDE_FROM_ALL src/sweep_test.cpp)
target_link_libraries(sweep_test PRIVATE rings_core)
