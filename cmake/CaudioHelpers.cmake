# CaudioHelpers.cmake — DRY helpers for caudio components
# Provides ca_set_warnings / ca_set_module_warnings and shared component scaffolding.

set(CAUDIO_WARNING_FLAGS
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)

function(ca_set_warnings tgt)
  target_compile_options(${tgt} PRIVATE ${CAUDIO_WARNING_FLAGS})
endfunction()

function(ca_set_module_warnings tgt)
  target_compile_options(${tgt} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-template-names-tu-local -Wno-expose-global-module-tu-local>)
endfunction()

function(caudio_add_component NAME)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "WITH_FFMPEG" "" "SOURCES;DEPS;INCLUDES")
  if(NOT ARG_SOURCES)
    message(FATAL_ERROR "caudio_add_component(${NAME}): SOURCES required")
  endif()
  add_library(caudio_${NAME} STATIC)
  add_library(caudio::${NAME} ALIAS caudio_${NAME})
  target_sources(caudio_${NAME} PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES ${ARG_SOURCES})
  target_include_directories(caudio_${NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  if(ARG_INCLUDES)
    target_include_directories(caudio_${NAME} PRIVATE ${ARG_INCLUDES})
  endif()
  if(ARG_DEPS)
    target_link_libraries(caudio_${NAME} PUBLIC ${ARG_DEPS})
  endif()
  if(ARG_WITH_FFMPEG)
    target_link_libraries(caudio_${NAME} PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
    target_compile_definitions(caudio_${NAME} PUBLIC CAUDIO_WITH_FFMPEG=1)
  endif()
  ca_set_warnings(caudio_${NAME})
endfunction()

function(caudio_add_shared_variant NAME)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "EXTRA_DEPS")
  add_library(caudio_${NAME}_shared SHARED)
  set_target_properties(caudio_${NAME}_shared PROPERTIES OUTPUT_NAME caudio_${NAME})
  add_library(caudio::${NAME}_shared ALIAS caudio_${NAME}_shared)
  target_sources(caudio_${NAME}_shared PRIVATE src/_stub/shared_stub.cpp)
  target_link_libraries(caudio_${NAME}_shared PUBLIC caudio::${NAME} ${ARG_EXTRA_DEPS})
  target_link_libraries(caudio_${NAME}_shared PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::${NAME}>)
  target_link_options(caudio_${NAME}_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)
endfunction()
