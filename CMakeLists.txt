cmake_minimum_required(VERSION 3.22)
project(RingsPlugin VERSION 0.3.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

option(RINGS_BUILD_PLUGIN "Build the AU/VST3 plugin (needs JUCE)" ON)
option(RINGS_BUILD_CHECK  "Build RingsCheck, the host-less test of the plugin layer and panel (needs JUCE)" OFF)
set(RINGS_JUCE_DIR "" CACHE PATH "Use a local JUCE folder instead of downloading it (optional)")

if(RINGS_BUILD_PLUGIN OR RINGS_BUILD_CHECK)
  if(RINGS_JUCE_DIR)
    add_subdirectory(${RINGS_JUCE_DIR} JUCE)
  else()
    include(FetchContent)
    FetchContent_Declare(JUCE
      GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
      GIT_TAG 8.0.6
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
  endif()

  # The panel's fonts (built into the plugin so it looks and measures the same on every Mac and in the check).
  juce_add_binary_data(RingsFonts
    HEADER_NAME AnmFontData.h
    NAMESPACE   AnmFontData
    SOURCES
      fonts/SpaceGrotesk-Regular.ttf
      fonts/SpaceGrotesk-Medium.ttf
      fonts/Marcellus-Regular.ttf)
endif()

if(RINGS_BUILD_PLUGIN)
# Plugin formats: AU + VST3 (+ Standalone for quick testing).
set(FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND FORMATS AU)
endif()

# The codes below (PLUGIN_MANUFACTURER_CODE, PLUGIN_CODE) and the target name must never change: Logic finds saved
# projects' plugins by them. Only the name shown (PRODUCT_NAME) changed, from "Unofficial Rings" to "Wrongs".
juce_add_plugin(Rings
  COMPANY_NAME "particledots"
  PLUGIN_MANUFACTURER_CODE Ptdt
  PLUGIN_CODE Ring
  FORMATS ${FORMATS}
  PRODUCT_NAME "Wrongs"
  PLUGIN_NAME "Wrongs"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicEffect   # lets Logic route MIDI to it as a "MIDI-controlled effect"
  COPY_PLUGIN_AFTER_BUILD TRUE)
endif()

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
if(RINGS_BUILD_PLUGIN)
target_sources(Rings PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp src/Controls.cpp src/Style.h src/Controls.h)
target_compile_definitions(Rings PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Rings PRIVATE
  rings_core
  RingsFonts
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)
endif()

# --- Headless test of the DSP wrapper:  cmake --build build --target engine_test && build/engine_test ---
add_executable(engine_test EXCLUDE_FROM_ALL src/engine_test.cpp)
target_link_libraries(engine_test PRIVATE rings_core)

# --- Model-switching regression test:  JUNK=0xFF build/sweep_test ---
add_executable(sweep_test EXCLUDE_FROM_ALL src/sweep_test.cpp)
target_link_libraries(sweep_test PRIVATE rings_core)

# --- Pitch Snap test (function, same pitch as Off at the snapped value, all models; JUCE-free):
#     cmake --build build --target pitch_snap_test && build/pitch_snap_test ---
add_executable(pitch_snap_test EXCLUDE_FROM_ALL src/pitch_snap_test.cpp)
target_link_libraries(pitch_snap_test PRIVATE rings_core)

# --- Processor-level test (the real parameters -> engine wiring, state; no host or window):  build/plugin_test ---
if(RINGS_BUILD_PLUGIN)
add_executable(plugin_test EXCLUDE_FROM_ALL src/plugin_test.cpp)
target_compile_definitions(plugin_test PRIVATE JucePlugin_Name="Wrongs" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(plugin_test PRIVATE Rings juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()

# --- RingsCheck: the plugin layer and the panel without a host (controls, saved projects, the panel and its pictures,
#     drawing cost). Not part of the plugin; the build workflow runs it on every push. ---
if(RINGS_BUILD_CHECK)
juce_add_console_app(RingsCheck PRODUCT_NAME "RingsCheck")
set_target_properties(RingsCheck PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(RingsCheck PRIVATE
  check/Check.cpp
  src/PluginProcessor.cpp
  src/PluginEditor.cpp
  src/Controls.cpp)
target_include_directories(RingsCheck PRIVATE src)
target_compile_definitions(RingsCheck PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_ALSA=0 JUCE_JACK=0 JUCE_MODAL_LOOPS_PERMITTED=0
  RINGS_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/check/golden"
  JucePlugin_Name="Wrongs")
target_link_libraries(RingsCheck
  PRIVATE
    rings_core
    RingsFonts
    juce::juce_audio_processors
    juce::juce_gui_basics
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
endif()
