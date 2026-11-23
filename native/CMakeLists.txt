# Ravakele: the engine as a CLAP plugin, and with clap-wrapper as VST3, AUv2 and a standalone app.
# See BUILD.md. third_party/clap (and third_party/clap-wrapper for the other formats) are yours to add.
cmake_minimum_required(VERSION 3.21)
project(ravakele VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "")

# The engine: the project's generated Engine.hpp on the fixed runtime and the DSP library.
add_library(ravakele_engine STATIC ../engine/lemnos_engine.cpp)
target_include_directories(ravakele_engine PUBLIC ../engine)

# The offline renderer: a WAV of one note, to check the engine on this machine.
add_executable(ravakele_render render.cpp)
target_link_libraries(ravakele_render PRIVATE ravakele_engine)

option(LEMNOS_CLAP "Build the CLAP plugin (needs third_party/clap)" ON)
option(LEMNOS_WRAPPED "Also build VST3, AUv2 and standalone with clap-wrapper (needs third_party/clap-wrapper)" OFF)

if (LEMNOS_CLAP)
  add_subdirectory(third_party/clap EXCLUDE_FROM_ALL)
  add_library(ravakele_clap MODULE plugin.cpp)
  target_link_libraries(ravakele_clap PRIVATE ravakele_engine clap)
  if (APPLE)
    set_target_properties(ravakele_clap PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION clap OUTPUT_NAME "Ravakele"
      MACOSX_BUNDLE_GUI_IDENTIFIER lemnos.ravakele MACOSX_BUNDLE_BUNDLE_VERSION 1.0.0)
  else()
    set_target_properties(ravakele_clap PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "Ravakele")
  endif()
endif()

if (LEMNOS_WRAPPED)
  # clap-wrapper's clap-first build: check its README for the version you add; its CMake API is theirs.
  set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE)
  add_subdirectory(third_party/clap-wrapper)
  add_library(ravakele_impl STATIC plugin.cpp)
  target_link_libraries(ravakele_impl PUBLIC ravakele_engine clap)
  make_clapfirst_plugins(
    TARGET_NAME ravakele
    IMPL_TARGET ravakele_impl
    OUTPUT_NAME "Ravakele"
    ENTRY_SOURCE plugin.cpp
    BUNDLE_IDENTIFIER lemnos.ravakele
    BUNDLE_VERSION 1.0.0
    PLUGIN_FORMATS VST3 AUV2 STANDALONE)
endif()
