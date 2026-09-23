cmake_minimum_required(VERSION 3.16)

project(midas_common_analyzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MIDASSYS "$ENV{MIDASSYS}" CACHE PATH "MIDAS installation prefix")
if(NOT MIDASSYS)
  set(MIDASSYS "/home/daq/midas/midas_src")
endif()

set(ROOTANA_DIR "/home/daq/midas/rootana" CACHE PATH "ROOTANA installation prefix")

find_package(ROOT REQUIRED COMPONENTS Core RIO Tree)

if(NOT EXISTS "${MIDASSYS}/lib/midas-targets.cmake")
  message(FATAL_ERROR "MIDAS CMake targets not found below MIDASSYS=${MIDASSYS}")
endif()
if(NOT EXISTS "${MIDASSYS}/lib/manalyzer-targets.cmake")
  message(FATAL_ERROR "manalyzer CMake targets not found below MIDASSYS=${MIDASSYS}")
endif()
if(NOT EXISTS "${ROOTANA_DIR}/lib/librootana.a")
  message(FATAL_ERROR "ROOTANA library not found: ${ROOTANA_DIR}/lib/librootana.a")
endif()

include("${MIDASSYS}/lib/midas-targets.cmake")
include("${MIDASSYS}/lib/manalyzer-targets.cmake")

add_library(rootana::rootana STATIC IMPORTED GLOBAL)
set_target_properties(rootana::rootana PROPERTIES
  IMPORTED_LOCATION "${ROOTANA_DIR}/lib/librootana.a"
  INTERFACE_INCLUDE_DIRECTORIES "${ROOTANA_DIR}/include"
)

ROOT_GENERATE_DICTIONARY(G__RootTreeTypes
  "${CMAKE_CURRENT_SOURCE_DIR}/include/RootTreeTypes.h"
  LINKDEF "${CMAKE_CURRENT_SOURCE_DIR}/include/RootTreeLinkDef.h"
)

add_executable(midas_analyzer
  src/analyzer_main.cxx
  src/EventInspector.cxx
  src/EventBuilder.cxx
  src/RootTreeWriter.cxx
  "${CMAKE_CURRENT_BINARY_DIR}/G__RootTreeTypes.cxx"
  decoder/V792Decoder.cxx
  decoder/V775Decoder.cxx
  decoder/V1190Decoder.cxx
  decoder/V1720Decoder.cxx
  decoder/EasirocDecoder.cxx
)

target_include_directories(midas_analyzer PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/decoder"
)

target_compile_options(midas_analyzer PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

target_link_libraries(midas_analyzer PRIVATE
  manalyzer_main
  rootana::rootana
)
