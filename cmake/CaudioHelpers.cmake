# CaudioHelpers.cmake -- DRY helpers for caudio components
# Provides ca_set_warnings / ca_set_module_warnings and shared component scaffolding.

if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL} AND NOT CMAKE_GENERATOR MATCHES "Ninja")
  set(CAUDIO_MSVC_MP_FLAG "/MP$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
else()
  set(CAUDIO_MSVC_MP_FLAG "")
endif()

# U-6b gate: our own targets treat use of the deprecated caudio::db:: /
# caudio::engine:: aliases as an error -- new internal code must spell
# caudio::types:: directly. GCC/Clang only: MSVC's C4996 also fires for CRT /
# secure-CRT deprecations, so /we4996 would be too broad (MSVC is CI-only;
# shipping compilers are GCC/Clang, where alias-use deprecation is verified).
set(CAUDIO_WARNING_FLAGS
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd4324 /WX ${CAUDIO_MSVC_MP_FLAG}>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror=deprecated-declarations -Werror>)

function(ca_set_warnings tgt)
  target_compile_options(${tgt} PRIVATE ${CAUDIO_WARNING_FLAGS})
endfunction()

function(ca_set_module_warnings tgt)
  if(CAUDIO_ENABLE_MODULES)
    target_compile_options(${tgt} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-template-names-tu-local -Wno-expose-global-module-tu-local -Wno-global-module>)
  endif()
endfunction()

# Append the CAUDIO_EXTRA_* cache strings to one of our own targets. Never
# call this on vendor targets, and never fold these into CMAKE_CXX_FLAGS /
# CMAKE_C_FLAGS / CMAKE_EXE_LINKER_FLAGS: the cache defaults stay untouched
# and an empty value is a no-op.
function(caudio_apply_extra_flags tgt)
  if(CAUDIO_EXTRA_CXX_FLAGS)
    separate_arguments(_caudio_extra_cxx NATIVE_COMMAND "${CAUDIO_EXTRA_CXX_FLAGS}")
    foreach(_flag IN LISTS _caudio_extra_cxx)
      target_compile_options(${tgt} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${_flag}>)
    endforeach()
  endif()
  if(CAUDIO_EXTRA_C_FLAGS)
    separate_arguments(_caudio_extra_c NATIVE_COMMAND "${CAUDIO_EXTRA_C_FLAGS}")
    foreach(_flag IN LISTS _caudio_extra_c)
      target_compile_options(${tgt} PRIVATE $<$<COMPILE_LANGUAGE:C>:${_flag}>)
    endforeach()
  endif()
  if(CAUDIO_EXTRA_LINK_FLAGS)
    separate_arguments(_caudio_extra_link NATIVE_COMMAND "${CAUDIO_EXTRA_LINK_FLAGS}")
    target_link_options(${tgt} PRIVATE ${_caudio_extra_link})
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
    # BASE_DIRS strips the leading modules/ so the install tree gets
    # <prefix>/modules/<component>/*.cppm (not modules/modules/...).
    target_sources(${NAME} PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/modules FILES ${ARG_MODULE_SOURCES})
    # A module interface unit must begin with `module;`/`export module`;
    # forcing the PCH (-include cmake_pch.hxx) ahead of it is ill-formed
    # (GCC rejects with "module-declaration only permitted as first
    # declaration"). Keep the PCH for regular TUs, skip it for .cppm.
    set_source_files_properties(${ARG_MODULE_SOURCES} TARGET_DIRECTORY ${NAME}
      PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
  endif()
  target_include_directories(${NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_include_directories(${NAME} PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
  if(ARG_INCLUDES)
    # SYSTEM: INCLUDES is always vendored third-party (vendor/) -- its
    # headers must not inherit our -Wall -Wextra -Wpedantic noise (notably
    # Clang's -Wlanguage-extension-token in sqlite3.h).
    target_include_directories(${NAME} SYSTEM PRIVATE ${ARG_INCLUDES})
  endif()
  # nlohmann is the opaque Json backend (vendor/, private include only). Every
  # component gets the same SYSTEM -I here so no component has to restate it;
  # linking it as a target would force it into install(EXPORT).
  if(CAUDIO_NLOHMANN_PRIVATE_INCLUDE)
    target_include_directories(${NAME} SYSTEM PRIVATE ${CAUDIO_NLOHMANN_PRIVATE_INCLUDE})
  endif()
  if(ARG_DEPS)
    target_link_libraries(${NAME} PUBLIC ${ARG_DEPS})
  endif()
  # Public C++23 requirement: headers use C++23 throughout, and components
  # ship CXX_MODULES interface units -- consumers (incl. module synth targets)
  # must compile at C++23 or newer.
  target_compile_features(${NAME} PUBLIC cxx_std_23)
  # Precompiled-header preamble: stable system headers compiled once per
  # target instead of re-parsed in every TU. Biggest win on MSVC.
  target_precompile_headers(${NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/caudio/pch.hpp)
  # FFmpeg (the decoder backend) is linked PRIVATE by the components that
  # actually use it (currently media; see media.cmake) -- not blanket-linked to
  # every component, which forced every target to resolve FFmpeg at link time
  # and pulled its include dirs into unrelated TUs.
  ca_set_warnings(${NAME})
  caudio_apply_extra_flags(${NAME})
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(${NAME})
  endif()
endfunction()

# D-4: header self-containment. Every public header is compiled as its own
# translation unit with only include/ on the search path -- no PCH and no
# other caudio headers pre-included -- so a header that silently leans on
# another header's includes (or on the component PCH) fails the build instead
# of leaking the requirement downstream. Compile-only: the objects are never
# linked. Policy: every include/caudio/**.hpp (and the caudio.hpp umbrella)
# must stand alone.
function(_caudio_write_if_different path content)
  set(_existing "")
  if(EXISTS "${path}")
    file(READ "${path}" _existing)
  endif()
  if(NOT "${_existing}" STREQUAL "${content}")
    file(WRITE "${path}" "${content}")
  endif()
endfunction()

function(caudio_add_header_self_containment)
  set(_hsc_inc "${CMAKE_CURRENT_SOURCE_DIR}/include")
  file(GLOB_RECURSE _hsc_headers CONFIGURE_DEPENDS "${_hsc_inc}/caudio/*.hpp")
  list(APPEND _hsc_headers "${_hsc_inc}/caudio.hpp")

  add_library(caudio_header_self_containment OBJECT)
  target_include_directories(caudio_header_self_containment PRIVATE
    "${_hsc_inc}"
    "${CMAKE_CURRENT_BINARY_DIR}/include")
  target_compile_features(caudio_header_self_containment PRIVATE cxx_std_23)
  ca_set_warnings(caudio_header_self_containment)

  set(_hsc_gen "${CMAKE_CURRENT_BINARY_DIR}/header_self_containment")
  file(MAKE_DIRECTORY "${_hsc_gen}")
  foreach(_hdr IN LISTS _hsc_headers)
    file(RELATIVE_PATH _rel "${_hsc_inc}" "${_hdr}")
    string(REPLACE "\\" "/" _rel "${_rel}")
    string(REPLACE "/" "_" _stem "${_rel}")
    string(REPLACE ".hpp" ".cpp" _stem "${_stem}")
    set(_tu "${_hsc_gen}/${_stem}")
    _caudio_write_if_different("${_tu}"
      "// D-4: standalone TU for <${_rel}> (generated).\n#include <${_rel}>\n")
    target_sources(caudio_header_self_containment PRIVATE "${_tu}")
  endforeach()
endfunction()

function(caudio_add_shared_variant NAME)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "EXTRA_DEPS")
  add_library(${NAME}_shared SHARED)
  set_target_properties(${NAME}_shared PROPERTIES OUTPUT_NAME ${NAME})
  add_library(caudio::${NAME}_shared ALIAS ${NAME}_shared)
  target_sources(${NAME}_shared PRIVATE src/_stub/shared_stub.cpp)
  target_link_libraries(${NAME}_shared PUBLIC ${ARG_EXTRA_DEPS})
  target_link_libraries(${NAME}_shared PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::${NAME}>)
  target_link_options(${NAME}_shared PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU>,$<NOT:$<PLATFORM_ID:Darwin>>>:-Wl,--allow-multiple-definition>)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(${NAME}_shared)
  endif()
endfunction()
