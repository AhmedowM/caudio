# CaudioHelpers.cmake -- DRY helpers for caudio components
# Provides ca_set_warnings / ca_set_module_warnings and shared component scaffolding.

set(CAUDIO_WARNING_FLAGS
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)

function(ca_set_warnings tgt)
  target_compile_options(${tgt} PRIVATE ${CAUDIO_WARNING_FLAGS})
endfunction()

function(ca_set_module_warnings tgt)
  if(CAUDIO_ENABLE_MODULES)
    target_compile_options(${tgt} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-template-names-tu-local -Wno-expose-global-module-tu-local>)
  endif()
endfunction()

function(caudio_add_component NAME)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;MODULE_SOURCES;DEPS;INCLUDES")
  if(NOT ARG_SOURCES AND NOT ARG_MODULE_SOURCES)
    message(FATAL_ERROR "caudio_add_component(${NAME}): SOURCES or MODULE_SOURCES required")
  endif()
  add_library(${NAME} STATIC)
  add_library(caudio::${NAME} ALIAS ${NAME})
  if(ARG_SOURCES)
    target_sources(${NAME} PRIVATE ${ARG_SOURCES})
  endif()
  if(ARG_MODULE_SOURCES AND CAUDIO_ENABLE_MODULES)
    target_sources(${NAME} PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES ${ARG_MODULE_SOURCES})
  endif()
  target_include_directories(${NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_include_directories(${NAME} PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
  if(ARG_INCLUDES)
    target_include_directories(${NAME} PRIVATE ${ARG_INCLUDES})
  endif()
  if(ARG_DEPS)
    target_link_libraries(${NAME} PUBLIC ${ARG_DEPS})
  endif()
  # Public C++23 requirement: headers use C++23 throughout, and components
  # ship CXX_MODULES interface units -- consumers (incl. module synth targets)
  # must compile at C++23 or newer.
  target_compile_features(${NAME} PUBLIC cxx_std_23)
  # FFmpeg is required (sole decoder backend); every component links it.
  target_link_libraries(${NAME} PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
  ca_set_warnings(${NAME})
endfunction()

function(caudio_add_shared_variant NAME)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "EXTRA_DEPS")
  add_library(${NAME}_shared SHARED)
  set_target_properties(${NAME}_shared PROPERTIES OUTPUT_NAME ${NAME})
  add_library(caudio::${NAME}_shared ALIAS ${NAME}_shared)
  target_sources(${NAME}_shared PRIVATE src/_stub/shared_stub.cpp)
  target_link_libraries(${NAME}_shared PUBLIC caudio::${NAME} ${ARG_EXTRA_DEPS})
  target_link_libraries(${NAME}_shared PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::${NAME}>)
  target_link_options(${NAME}_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)
endfunction()
