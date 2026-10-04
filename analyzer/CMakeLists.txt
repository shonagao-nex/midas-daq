cmake_minimum_required(VERSION 3.16)

project(midas_common_analyzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MIDASSYS "$ENV{MIDASSYS}" CACHE PATH "MIDAS installation prefix")
if(NOT MIDASSYS)
  set(MIDASSYS "$ENV{HOME}/midas/midas_src")
endif()

set(ROOTANA_DIR "$ENV{HOME}/midas/rootana" CACHE PATH "ROOTANA installation prefix")

find_package(ROOT REQUIRED COMPONENTS Core RIO Tree Hist Graf Gpad)

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

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
add_executable(midas_analyzer
  src/analyzer_main.cxx
  src/AnalyzerCli.cxx
  src/EventInspector.cxx
  src/EventDiagnostics.cxx
  src/EventBuilder.cxx
  src/RootTreeWriter.cxx
  src/ExpressionResolver.cxx
  src/HistogramConfig.cxx
  src/HistogramConfigLoader.cxx
  src/HistogramOdbInitializer.cxx
  src/HistogramManager.cxx
  src/OnlineHistogramState.cxx
  src/OnlineHistogramPrescale.cxx
  src/HistogramPdfWriter.cxx
  src/PageConfig.cxx
  src/PageConfigLoader.cxx
  src/PageManager.cxx
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

# ROOT looks for the executable's dictionary PCM beside the executable.
# Keep the generated PCM in build/ and expose only a runtime link in bin/.
add_custom_command(TARGET midas_analyzer POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E create_symlink
    "${CMAKE_CURRENT_BINARY_DIR}/libRootTreeTypes_rdict.pcm"
    "${CMAKE_CURRENT_SOURCE_DIR}/bin/libRootTreeTypes_rdict.pcm"
)

enable_testing()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/test")
add_executable(expression_resolver_test
  tests/ExpressionResolverTest.cxx
  src/ExpressionResolver.cxx
)
target_include_directories(expression_resolver_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(expression_resolver_test PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)
add_test(NAME expression_resolver_test COMMAND expression_resolver_test)

add_executable(histogram_config_validation_test
  tests/HistogramConfigValidationTest.cxx
  src/HistogramConfig.cxx
  src/ExpressionResolver.cxx
)
target_include_directories(histogram_config_validation_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(histogram_config_validation_test PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)
add_test(NAME histogram_config_validation_test
  COMMAND histogram_config_validation_test
)

add_executable(histogram_manager_test
  tests/HistogramManagerTest.cxx
  src/HistogramManager.cxx
  src/HistogramConfig.cxx
  src/ExpressionResolver.cxx
)
target_include_directories(histogram_manager_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(histogram_manager_test PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)
target_link_libraries(histogram_manager_test PRIVATE
  ROOT::Core
  ROOT::RIO
  ROOT::Hist
)
add_test(NAME histogram_manager_test COMMAND histogram_manager_test)

add_executable(analyzer_cli_test
  tests/AnalyzerCliTest.cxx
  src/AnalyzerCli.cxx
)
target_include_directories(analyzer_cli_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(analyzer_cli_test PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)
add_test(NAME analyzer_cli_test COMMAND analyzer_cli_test)

add_executable(event_diagnostics_test
  tests/EventDiagnosticsTest.cxx
  src/EventDiagnostics.cxx
)
target_include_directories(event_diagnostics_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(event_diagnostics_test PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)
target_link_libraries(event_diagnostics_test PRIVATE rootana::rootana midas::midas)
add_test(NAME event_diagnostics_test COMMAND event_diagnostics_test)

add_executable(event_builder_test
  tests/EventBuilderTest.cxx
  src/EventBuilder.cxx
  src/EventDiagnostics.cxx
  decoder/V792Decoder.cxx
  decoder/V775Decoder.cxx
  decoder/V1190Decoder.cxx
  decoder/V1720Decoder.cxx
  decoder/EasirocDecoder.cxx
)
target_include_directories(event_builder_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/decoder"
)
target_compile_options(event_builder_test PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(event_builder_test PRIVATE rootana::rootana midas::midas)
add_test(NAME event_builder_test COMMAND event_builder_test)

add_executable(histogram_config_loader_test
  tests/HistogramConfigLoaderTest.cxx
  src/PageConfig.cxx
  src/PageConfigLoader.cxx
  src/HistogramConfigLoader.cxx
  src/HistogramOdbInitializer.cxx
  src/OnlineHistogramPrescale.cxx
  src/HistogramConfig.cxx
  src/ExpressionResolver.cxx
)
target_include_directories(histogram_config_loader_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(histogram_config_loader_test PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)
target_link_libraries(histogram_config_loader_test PRIVATE
  midas::midas
)
add_test(NAME histogram_config_loader_test
  COMMAND histogram_config_loader_test
)

add_executable(histogram_pdf_writer_test
  tests/HistogramPdfWriterTest.cxx
  src/HistogramPdfWriter.cxx
  src/HistogramManager.cxx
  src/HistogramConfig.cxx
  src/ExpressionResolver.cxx
)
target_include_directories(histogram_pdf_writer_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(histogram_pdf_writer_test PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)
target_link_libraries(histogram_pdf_writer_test PRIVATE
  ROOT::Core
  ROOT::RIO
  ROOT::Hist
  ROOT::Graf
  ROOT::Gpad
)
add_test(NAME histogram_pdf_writer_test COMMAND histogram_pdf_writer_test)

add_executable(page_manager_test
  tests/PageManagerTest.cxx
  src/PageConfig.cxx
  src/PageConfigLoader.cxx
  src/PageManager.cxx
  src/HistogramManager.cxx
  src/HistogramConfig.cxx
  src/ExpressionResolver.cxx
)
target_include_directories(page_manager_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(page_manager_test PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)
target_link_libraries(page_manager_test PRIVATE
  ROOT::Core
  ROOT::RIO
  ROOT::Hist
  ROOT::Graf
  ROOT::Gpad
  midas::midas
)
add_test(NAME page_manager_test COMMAND page_manager_test)

add_executable(online_histogram_state_test
  tests/OnlineHistogramStateTest.cxx
  src/OnlineHistogramState.cxx
  src/HistogramManager.cxx
  src/HistogramPdfWriter.cxx
  src/PageManager.cxx
  src/PageConfig.cxx
  src/HistogramConfig.cxx
  src/ExpressionResolver.cxx
)
target_include_directories(online_histogram_state_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_options(online_histogram_state_test PRIVATE
  -Wall -Wextra -Wpedantic
)
target_link_libraries(online_histogram_state_test PRIVATE
  midas::midas ROOT::Core ROOT::RIO ROOT::Hist ROOT::Graf ROOT::Gpad
)
add_test(NAME online_histogram_state_test COMMAND online_histogram_state_test)
