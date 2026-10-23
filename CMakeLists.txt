cmake_minimum_required(VERSION 3.28)

# Version precedence (top wins). Release candidates ride along as an optional
# semver suffix (e.g. v1.0.0-rc1): the numeric core feeds project()/package
# machinery (numbers only), the full string feeds display (--version, CPack
# names). Suffix is derived from the tag/override/file -- never typed twice.
#   1. -DCAUDIO_PROJECT_VERSION=X.Y.Z[-suffix] (explicit override for packagers stripping .git)
#   2. git describe --tags (normal clones, CI with fetch-depth: 0)
#   3. VERSION file (tarballs, shallow exports -- bumped in the release-prep commit)
#   4. 0.1.0 + loud warning (never silent; NOT a release build)
option(CAUDIO_REQUIRE_GIT_VERSION "fail configure when no git tag is found (default OFF)" OFF)
set(CAUDIO_VERSION_SUFFIX "")
macro(_caudio_split_version INPUT OUT_OK)
  if("${INPUT}" MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.-]+)?$")
    set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}")
    set(PROJECT_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    set(CAUDIO_VERSION_SUFFIX "${CMAKE_MATCH_4}")
    set(${OUT_OK} TRUE)
  else()
    set(${OUT_OK} FALSE)
  endif()
endmacro()
if(DEFINED CAUDIO_PROJECT_VERSION AND NOT CAUDIO_PROJECT_VERSION STREQUAL "")
  _caudio_split_version("${CAUDIO_PROJECT_VERSION}" _caudio_ver_ok)
  if(NOT _caudio_ver_ok)
    message(FATAL_ERROR "CAUDIO_PROJECT_VERSION='${CAUDIO_PROJECT_VERSION}' is not X.Y.Z[-suffix] (optional leading v)")
  endif()
  set(GIT_TAG "v${PROJECT_VERSION}${CAUDIO_VERSION_SUFFIX}")
  message(STATUS "Using explicit version override: ${PROJECT_VERSION}${CAUDIO_VERSION_SUFFIX} (-DCAUDIO_PROJECT_VERSION)")
else()
  execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  _caudio_split_version("${GIT_TAG}" _caudio_ver_ok)
  if(NOT _caudio_ver_ok)
    set(GIT_TAG "")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION_FILE_CONTENT)
    string(STRIP "${VERSION_FILE_CONTENT}" VERSION_FILE_CONTENT)
    _caudio_split_version("${VERSION_FILE_CONTENT}" _caudio_ver_ok)
    if(_caudio_ver_ok)
      set(GIT_TAG "v${PROJECT_VERSION}${CAUDIO_VERSION_SUFFIX}")
      message(STATUS "No git tag -- using VERSION file: ${PROJECT_VERSION}${CAUDIO_VERSION_SUFFIX}")
    else()
      if(CAUDIO_REQUIRE_GIT_VERSION)
        message(FATAL_ERROR "CAUDIO_REQUIRE_GIT_VERSION=ON but no git tag found -- clone with tags (git fetch --tags) or configure with -DCAUDIO_REQUIRE_GIT_VERSION=OFF")
      endif()
      message(WARNING "No git tag and no usable VERSION file -- using fallback version 0.1.0. Tag-less builds are NOT release builds; docs/CHANGELOG claim the latest tagged release.")
      set(PROJECT_VERSION_MAJOR "0")
      set(PROJECT_VERSION_MINOR "1")
      set(PROJECT_VERSION_PATCH "0")
      set(PROJECT_VERSION "0.1.0")
      set(CAUDIO_VERSION_SUFFIX "")
    endif()
  endif()
endif()
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE CAUDIO_GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT CAUDIO_GIT_HASH)
  set(CAUDIO_GIT_HASH "unknown")
endif()
set(CAUDIO_VERSION_FULL "${PROJECT_VERSION}")
if(GIT_TAG)
  set(CAUDIO_VERSION_FULL "${GIT_TAG}")
endif()

project(caudio VERSION ${PROJECT_VERSION} LANGUAGES CXX C)

# Install dirs first: CaudioHelpers forms `$<INSTALL_INTERFACE:...>` entries
# from these variables at target-declaration time (eager expansion), so they
# must exist before any component is declared -- previously masked by
# nlohmann's own include(GNUInstallDirs) running earlier via FetchContent.
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(CAUDIO_ENABLE_MODULES "build/install C++23 modules (optional, headers are canonical)" OFF)
if(CAUDIO_ENABLE_MODULES)
  set(CMAKE_CXX_SCAN_FOR_MODULES ON)
else()
  set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
endif()
# Single compilation per C++23 module: static libs own the module BMIs,
# shared variants link the static objects whole-archive (GCC -fmodules-ts
# BMI CRC covers defines/flags, so recompiling the same sources with
# different *_EXPORTS/PIC flags breaks dependents with CRC mismatch).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Compiler policy (default-per-host selection lives in the toolchain
# dispatcher via cmake/toolchains/auto.cmake; this is the backstop for raw
# configures).
include(cmake/CompilerPolicy.cmake)

# Windows/Clang-or-MSVC: silence "unsafe CRT" deprecation noise (C4996 on
# fopen/getenv/_wfopen, -Wdeprecated-declarations under Clang). The portable
# calls are correct; the _s variants would fork the code path for
# non-shipping compilers. CI/Release is GCC-only, so this affects local
# Windows builds only.
if(MSVC OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND WIN32))
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Options -- FFmpeg is required primary (dr_* deleted, vendor cleaned)
option(CAUDIO_WITH_FETCH_FFMPEG "auto-fetch FFmpeg if not found (system -> vcpkg/Conan -> prebuilt -> source)" ON)
option(CAUDIO_USE_TRIMMED_FFMPEG "build against a trimmed decode-only FFmpeg (cmake/FFmpegTrimmed.cmake whitelist)" OFF)
option(CAUDIO_ENABLE_TESTS "build tests" OFF)
option(CAUDIO_ENABLE_EXAMPLES "build examples" OFF)
option(CAUDIO_ENABLE_SANITIZERS "Enable ASan+UBSan (like C: -fsanitize=address,undefined)" OFF)
option(CAUDIO_BUILD_DOCS "Build Doxygen documentation" OFF)
option(CAUDIO_TEST_NOAUDIO "skip audio device tests (no beep) for headless CI" OFF)
option(CAUDIO_ENABLE_CLANG_TIDY "enable clang-tidy checks during build" OFF)
option(CAUDIO_ENABLE_CCACHE "use ccache/sccache compiler launcher if found (opt-in)" OFF)
# Shared variants (*_shared + combined) use WHOLE_ARCHIVE + --allow-multiple-definition
# to work around duplicate-symbol/ODR issues from merged static twins. Static +
# header-only is the default; opt in only if you need shared libs.
# Status: EXPERIMENTAL and OFF by default — no shipped artifact uses shared libs.
option(CAUDIO_BUILD_SHARED "build shared *_shared variants + combined libcaudio" OFF)
option(CAUDIO_BUNDLE_FFMPEG_MACOS "bundle brew FFmpeg dylibs with @rpath fixup (standalone mac archives)" ON)
# Section GC for the minsize-lto presets. Apple ld rejects --gc-sections at
# compiler validation, so presets carry only the intent and the per-linker
# flags live here. GNU/Clang also need function/data sections at compile time;
# MSVC links /OPT:REF by default (/Gy only splits functions).
option(CAUDIO_LINK_SECTION_GC "drop unreferenced code/data sections at link (Apple -dead_strip, otherwise --gc-sections)" OFF)
# Extra flags appended to our own targets only (see caudio_apply_extra_flags).
# These never replace CMAKE_CXX_FLAGS / CMAKE_C_FLAGS / CMAKE_EXE_LINKER_FLAGS
# and the empty default is a no-op. Vendor/third-party targets are unaffected.
set(CAUDIO_EXTRA_CXX_FLAGS "" CACHE STRING "extra C++ compile flags for caudio targets")
set(CAUDIO_EXTRA_C_FLAGS "" CACHE STRING "extra C compile flags for caudio targets")
set(CAUDIO_EXTRA_LINK_FLAGS "" CACHE STRING "extra link flags for caudio targets")
if(CAUDIO_LINK_SECTION_GC)
  if(APPLE)
    add_link_options(-Wl,-dead_strip)
  elseif(MSVC)
    add_compile_options(/Gy)
  else()
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
  endif()
endif()
# NOTE: ccache/sccache is opt-in via CAUDIO_ENABLE_CCACHE; never hard-required
# so user builds don't crash when the launcher is missing.
if(CAUDIO_ENABLE_CCACHE)
  foreach(_launcher ccache sccache)
    find_program(_CAUDIO_LAUNCHER ${_launcher})
    if(_CAUDIO_LAUNCHER)
      set(CMAKE_CXX_COMPILER_LAUNCHER ${_CAUDIO_LAUNCHER})
      set(CMAKE_C_COMPILER_LAUNCHER ${_CAUDIO_LAUNCHER})
      message(STATUS "Compiler launcher: ${_CAUDIO_LAUNCHER}")
      break()
    endif()
  endforeach()
  if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
    message(WARNING "CAUDIO_ENABLE_CCACHE=ON but neither ccache nor sccache found -- compiling without launcher")
  endif()
endif()

# clang-tidy integration (requires compile_commands.json)
if(CAUDIO_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_PROGRAM clang-tidy)
  if(CLANG_TIDY_PROGRAM)
    set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_PROGRAM})
    message(STATUS "clang-tidy enabled: ${CLANG_TIDY_PROGRAM}")
  else()
    message(WARNING "CAUDIO_ENABLE_CLANG_TIDY=ON but clang-tidy not found")
  endif()
endif()

# Module path for FindFFmpeg
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Threads REQUIRED)

# Trimmed-first: a valid CAUDIO_TRIMMED_ROOT (cache -D or env) auto-selects
# the trimmed provider. An explicit FFmpeg_ROOT still wins; against an
# explicit -DCAUDIO_USE_TRIMMED_FFMPEG=OFF the root wins (announced below).
if(NOT FFmpeg_ROOT AND NOT "$ENV{FFmpeg_ROOT}")
  if(DEFINED CAUDIO_TRIMMED_ROOT AND EXISTS "${CAUDIO_TRIMMED_ROOT}/lib")
    set(_trim_root "${CAUDIO_TRIMMED_ROOT}")
  elseif(DEFINED ENV{CAUDIO_TRIMMED_ROOT} AND EXISTS "$ENV{CAUDIO_TRIMMED_ROOT}/lib")
    set(_trim_root "$ENV{CAUDIO_TRIMMED_ROOT}")
  endif()
  if(DEFINED _trim_root)
    set(FFmpeg_ROOT "${_trim_root}" CACHE PATH "FFmpeg root (auto-selected from CAUDIO_TRIMMED_ROOT)" FORCE)
    set(CAUDIO_USE_TRIMMED_FFMPEG ON CACHE BOOL "trimmed FFmpeg (auto-selected from CAUDIO_TRIMMED_ROOT)" FORCE)
    message(STATUS "Trimmed-first: CAUDIO_TRIMMED_ROOT=${_trim_root}")
  endif()
  unset(_trim_root)
endif()

function(caudio_ffmpeg_fatal)
  message(FATAL_ERROR "FFmpeg is required -- install FFmpeg (tools + dev packages) or set FFmpeg_ROOT")
endfunction()

if(NOT CAUDIO_USE_TRIMMED_FFMPEG)
  find_package(FFmpeg QUIET)
endif()
if(FFmpeg_FOUND AND NOT CAUDIO_USE_TRIMMED_FFMPEG)
  message(STATUS "FFmpeg found (system): ${FFmpeg_AVCODEC_LIBRARY}")
elseif(CAUDIO_USE_TRIMMED_FFMPEG)
  include(cmake/FFmpegTrimmed.cmake)
  caudio_setup_trimmed_ffmpeg()
  if(NOT FFmpeg_FOUND)
    caudio_ffmpeg_fatal()
  endif()
  message(STATUS "FFmpeg found (trimmed): ${FFmpeg_AVCODEC_LIBRARY}")
elseif(CAUDIO_WITH_FETCH_FFMPEG)
  include(cmake/SetupFFmpeg.cmake)
  caudio_setup_ffmpeg()
  if(NOT FFmpeg_FOUND)
    caudio_ffmpeg_fatal()
  endif()
  message(STATUS "FFmpeg found via fallback: ${FFmpeg_AVCODEC_LIBRARY}")
else()
  caudio_ffmpeg_fatal()
endif()

# Record the ABI floor for the installed config so downstream consumers link
# against the same FFmpeg major instead of silently re-finding another one.
function(caudio_detect_ffmpeg_avcodec_major out)
  set(_header "${FFmpeg_AVCODEC_INCLUDE_DIR}/libavcodec/version_major.h")
  if(NOT EXISTS "${_header}")
    set(_header "${FFmpeg_AVCODEC_INCLUDE_DIR}/libavcodec/version.h")
  endif()
  set(_major "")
  if(EXISTS "${_header}")
    file(STRINGS "${_header}" _line REGEX "^#define[ \t]+LIBAVCODEC_VERSION_MAJOR[ \t]+[0-9]+")
    string(REGEX MATCH "[0-9]+" _major "${_line}")
  endif()
  set(${out} "${_major}" PARENT_SCOPE)
endfunction()
caudio_detect_ffmpeg_avcodec_major(CAUDIO_FFMPEG_AVCODEC_MAJOR)

function(caudio_apply_sanitizers tgt)
  if(MSVC)
    target_compile_options(${tgt} PRIVATE /fsanitize=address)
    target_link_options(${tgt} PRIVATE /fsanitize=address)
  else()
    target_compile_options(${tgt} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    target_link_options(${tgt} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

if(CAUDIO_ENABLE_SANITIZERS)
  if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR "CAUDIO_ENABLE_SANITIZERS is not supported with ${CMAKE_CXX_COMPILER_ID} on Windows (MinGW has no working ASan/UBSan runtime). Use Linux, or MSVC for ASan only.")
  elseif(MSVC)
    message(STATUS "Sanitizers enabled: MSVC ASan (/fsanitize=address)")
  else()
    message(STATUS "Sanitizers enabled: ASan+UBSan")
  endif()
endif()

# Configure version header (template lives in cmake/ -- canonical for CMake @ONLY files)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/caudio/version_config.hpp
  @ONLY
)

# Vendor: sqlite3 STATIC (libsqlite3.a -- sqlite3 amalgamation, SQLITE_ENABLE_FTS5=1)
# Content-named like blake3 below; the only artifact called "caudio" is the
# opt-in shared umbrella (combined -> libcaudio).
add_library(sqlite3 STATIC vendor/sqlite3.c)
add_library(caudio::sqlite3 ALIAS sqlite3)
set_target_properties(sqlite3 PROPERTIES OUTPUT_NAME sqlite3)
target_include_directories(sqlite3 SYSTEM PRIVATE vendor)
# BUILD_INTERFACE only: vendor headers are NOT installed (nothing installed
# references them), so no INSTALL_INTERFACE -- a dangling install path breaks
# downstream configure with "non-existent path" errors.
# SYSTEM: vendored amalgamation -- dependents (db/*.cpp include sqlite3.h)
# must not inherit its extension-token noise under Clang. Propagates.
target_include_directories(sqlite3 SYSTEM PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor>)
target_compile_definitions(sqlite3 PUBLIC SQLITE_ENABLE_FTS5=1)
# Trimmed amalgamation: only the API surface
# in use (prepare/bind/step/column/finalize, exec, open_v2, WAL + foreign_keys
# + FTS5). OMIT_LOAD_EXTENSION is also hardening. None of the omitted areas
# (JSON1 SQL, authorizer, progress callbacks, get_table, loadable extensions,
# deprecated spellings) is referenced anywhere in src/tests/examples/cli.
target_compile_definitions(sqlite3 PUBLIC
  SQLITE_OMIT_LOAD_EXTENSION=1
  SQLITE_OMIT_DEPRECATED=1
  SQLITE_OMIT_AUTHORIZER=1
  SQLITE_OMIT_PROGRESS_CALLBACK=1
  SQLITE_OMIT_GET_TABLE=1
  SQLITE_OMIT_JSON=1
)

# Vendor: BLAKE3 C core (portable only - no AVX/SSE/NEON)
add_library(blake3 STATIC vendor/blake3.c vendor/blake3_portable.c)
target_include_directories(blake3 SYSTEM PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor>)
target_compile_definitions(blake3 PRIVATE BLAKE3_NO_AVX512 BLAKE3_NO_AVX2 BLAKE3_NO_SSE41 BLAKE3_NO_SSE2 BLAKE3_USE_NEON=0)
# suppress GCC warnings in vendored C
if(NOT MSVC)
  target_compile_options(blake3 PRIVATE -w)
endif()

# FetchContent deps needed before components. CLI11/Catch2 are lazy: prefer
# a system/vcpkg/Conan package first so plain configures don't pay for a git
# clone; fetch only as fallback. nlohmann_json is vendored instead
# (vendor/nlohmann/, see vendor/README.md): it is PRIVATE (backend for
# src/utils/json.cpp only) -- its headers never enter installed headers, so
# no JSON_Install and no find_dependency downstream. Components get it as
# PRIVATE SYSTEM -I (CAUDIO_NLOHMANN_PRIVATE_INCLUDE): even a PRIVATE target
# link would force it into install(EXPORT).
include(FetchContent)
# Vendored single-header backend, version locked in vendor/README.md.
set(CAUDIO_NLOHMANN_PRIVATE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

# CLI11 is private to the caudio executable: skip entirely with CAUDIO_BUILD_CLI=OFF.
option(CAUDIO_BUILD_CLI "build the caudio executable (requires CLI11)" ON)
if(CAUDIO_BUILD_CLI)
  find_package(CLI11 QUIET)
  if(NOT CLI11_FOUND)
    FetchContent_Declare(
      CLI11
      GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
      GIT_TAG v2.7.2
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(CLI11)
    set(CAUDIO_CLI11_EXTRA_INCLUDE ${CLI11_SOURCE_DIR}/include)
  else()
    message(STATUS "CLI11 found (system): includes come from CLI11::CLI11")
    set(CAUDIO_CLI11_EXTRA_INCLUDE "")
  endif()
endif()

# DRY helpers + component split (include preserves single-BMI build)
# TagLib is vendored like sqlite3/blake3 (always fetched, never system):
# private file-tag implementation detail, installed as caudio::tag next to
# them so install(EXPORT) resolves. Headers never enter installed headers.
FetchContent_Declare(
  TagLib
  GIT_REPOSITORY https://github.com/taglib/taglib.git
  GIT_TAG v2.3.2
  GIT_SHALLOW TRUE
)
  set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_CppUnit TRUE CACHE BOOL "" FORCE)
# No zlib: TagLib would otherwise pick up platform SDK headers (macOS),
# poisoning brew-libc++ resolution under our -nostdinc++ toolchain, and
# silently vary by host. Compressed ID3v2 frames are rare; builds without
# them read such frames as absent. Deterministic everywhere.
set(WITH_ZLIB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(TagLib)
message(STATUS "TagLib fetched (v2.3.2): file tag writing enabled")
# Headers stay private (like sqlite3.h/blake3.h): clear the PUBLIC_HEADER
# property so install() ships the archive only, without warnings.
set_target_properties(tag PROPERTIES PUBLIC_HEADER "")
if(NOT MSVC)
  # Vendored-style silence: TagLib's own headers trip -Wall -Wextra
  # (e.g. -Wmicrosoft-unqualified-friend); same -w treatment as blake3.
  # MSVC builds keep their (quiet) defaults.
  target_compile_options(tag PRIVATE -w)
endif()
# TagLib's `tag` target exports install includes only; consumers use flat
# per-subdir includes (`#include <apetag.h>`). Derive every header dir
# from the pinned source.
file(GLOB_RECURSE _taglib_hdrs "${taglib_SOURCE_DIR}/taglib/*.h")
set(CAUDIO_TAGLIB_BUILD_INCLUDES "${taglib_SOURCE_DIR}" "${taglib_BINARY_DIR}")
foreach(_h IN LISTS _taglib_hdrs)
  get_filename_component(_d "${_h}" DIRECTORY)
  list(APPEND CAUDIO_TAGLIB_BUILD_INCLUDES "${_d}")
endforeach()
list(REMOVE_DUPLICATES CAUDIO_TAGLIB_BUILD_INCLUDES)
include(cmake/CaudioHelpers.cmake)
include(cmake/components/types.cmake)
include(cmake/components/utils.cmake)
include(cmake/components/config.cmake)
include(cmake/components/media.cmake)
include(cmake/components/player.cmake)
include(cmake/components/db.cmake)
include(cmake/components/engine.cmake)
include(cmake/components/ipc.cmake)
include(cmake/components/service.cmake)
include(cmake/components/client.cmake)
include(cmake/components/cli.cmake)
include(cmake/components/app.cmake)
include(cmake/components/combined.cmake)

# Umbrella module (optional, headers are canonical)
if(CAUDIO_ENABLE_MODULES)
  add_library(caudio_umbrella STATIC)
  target_sources(caudio_umbrella PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/modules FILES modules/caudio.cppm)
  target_include_directories(caudio_umbrella PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
  target_link_libraries(caudio_umbrella PUBLIC caudio::types caudio::utils caudio::config caudio::media caudio::player caudio::db caudio::engine caudio::ipc caudio::service caudio::client caudio::app)
  ca_set_warnings(caudio_umbrella)
  ca_set_module_warnings(caudio_umbrella)
  caudio_apply_extra_flags(caudio_umbrella)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(caudio_umbrella)
  endif()
endif()

# Documentation (Doxygen) -- see cmake/Docs.cmake
if(CAUDIO_BUILD_DOCS)
  include(cmake/Docs.cmake)
  caudio_add_docs()
endif()

# Install / Export / Packaging like C CMakeLists.txt:334-382
# (GNUInstallDirs included once, right after project() above.)
set(CAUDIO_INSTALL_LIBS
  types
  utils
  config
  media
  player
  db
  engine
  ipc
  client
  service
  app
  sqlite3
  blake3
  tag
)
if(CAUDIO_BUILD_SHARED)
  list(APPEND CAUDIO_INSTALL_LIBS
    utils_shared
    media_shared
    player_shared
    db_shared
    engine_shared
    client_shared
    service_shared
    app_shared
    combined
  )
endif()
if(CAUDIO_ENABLE_MODULES)
  install(
    TARGETS ${CAUDIO_INSTALL_LIBS}
    EXPORT caudioTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT dev
    # Module sources live at <prefix>/modules/ (NOT under include/):
    # they are build inputs for BMI consumers, not C++ headers.
    FILE_SET CXX_MODULES DESTINATION modules COMPONENT dev
  )
else()
  install(
    TARGETS ${CAUDIO_INSTALL_LIBS}
    EXPORT caudioTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dev
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT dev
  )
endif()
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/caudio/version_config.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/caudio COMPONENT dev)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT dev FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")

# NOTE: vendor headers (sqlite3.h, blake3.h) are deliberately NOT installed --
# no public header references them (fwd-decls only). Only the static archives
# below ship (transitive link deps). miniaudio.h was never installed either.
install(FILES cmake/FindFFmpeg.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caudio COMPONENT dev)
install(EXPORT caudioTargets NAMESPACE caudio:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caudio COMPONENT dev)

include(CMakePackageConfigHelpers)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/caudioConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/caudioConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caudio
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/caudioConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/caudioConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/caudioConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caudio
  COMPONENT dev
)

if(CAUDIO_BUILD_CLI)
  install(TARGETS caudio_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
endif()

# Windows distributability: the caudio.exe links FFmpeg + MinGW runtime
# dynamically, none of which lives on a bare machine's PATH. Bundle both next
# to the executable so CPack archives run standalone. (macOS bundles behind
# CAUDIO_BUNDLE_FFMPEG_MACOS; Linux is bundled unconditionally below.)
if(WIN32 AND CAUDIO_BUILD_CLI)
  # Bundle FFmpeg runtime DLLs next to the executable. Our FindFFmpeg targets
  # are UNKNOWN IMPORTED (no runtime location), so resolve the DLLs by layout:
  # <root>/bin next to the found import library (gyan/choco/vcpkg layouts).
  get_filename_component(_ff_libdir "${FFmpeg_AVCODEC_LIBRARY}" DIRECTORY)
  file(GLOB _ff_dlls
    "${_ff_libdir}/../bin/avcodec-*.dll"
    "${_ff_libdir}/../bin/avformat-*.dll"
    "${_ff_libdir}/../bin/avutil-*.dll"
    "${_ff_libdir}/../bin/*swresample-*.dll")
  if(_ff_dlls)
    install(FILES ${_ff_dlls} DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
  else()
    message(WARNING "FFmpeg DLLs not found near ${FFmpeg_AVCODEC_LIBRARY} -- packages will need them on PATH")
  endif()
  if(MINGW)
    # Only winpthread ships as a DLL: libstdc++/libgcc are statically linked
    # into the exe above. winpthread stays shared on purpose -- FFmpeg's own
    # DLLs use it too, and two pthread runtimes in one process is worse than
    # one shared DLL.
    get_filename_component(_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    file(GLOB _mingw_rt_dlls "${_mingw_bin}/libwinpthread-1.dll")
    if(_mingw_rt_dlls)
      install(FILES ${_mingw_rt_dlls} DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
    else()
      message(WARNING "libwinpthread-1.dll not found next to ${CMAKE_CXX_COMPILER} -- packages will need it on PATH")
    endif()
  endif()
endif()

# Linux distributability: ship the versioned .so files (SONAME links
# included -- the loader asks for libavcodec.so.62 by exact name) in lib/
# with an $ORIGIN-relative RPATH. The RPATH goes on the target property:
# CMAKE_INSTALL_RPATH is consumed at target creation, so the variable is
# too late here.
if(UNIX AND NOT APPLE AND CAUDIO_BUILD_CLI)
  get_filename_component(_ff_libdir "${FFmpeg_AVCODEC_LIBRARY}" DIRECTORY)
  set(_ff_sos "")
  foreach(_lib avcodec avformat avutil swresample)
    file(GLOB _cand "${_ff_libdir}/lib${_lib}.so.*")
    list(APPEND _ff_sos ${_cand})
  endforeach()
  list(REMOVE_DUPLICATES _ff_sos)
  if(NOT _ff_sos)
    if(NOT FFmpeg_AVCODEC_LIBRARY MATCHES "\\.a$")
      message(WARNING "FFmpeg shared libs not found near ${FFmpeg_AVCODEC_LIBRARY} -- packages will need them on the loader path")
    endif()
  else()
    install(FILES ${_ff_sos} DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime)
    get_target_property(_cli_rpath caudio_cli INSTALL_RPATH)
    if(_cli_rpath STREQUAL "_cli_rpath-NOTFOUND")
      set(_cli_rpath "")
    endif()
    list(APPEND _cli_rpath "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    set_target_properties(caudio_cli PROPERTIES INSTALL_RPATH "${_cli_rpath}")
  endif()
endif()

# macOS distributability (UNVERIFIED -- no macOS host as of 2026-10-09; keep
# CAUDIO_BUNDLE_FFMPEG_MACOS=OFF until validated on a Mac or the macOS
# release leg). Like Windows, the exe links FFmpeg dynamically -- but brew
# dylibs carry absolute install names, so a bare archive fails on machines
# without brew ffmpeg. When enabled, versioned dylibs ship in lib/ and an
# install-time script rewires ids + references to @rpath and re-signs
# ad-hoc (install_name_tool invalidates signatures; without re-sign the
# kernel refuses the binaries on Apple Silicon).
if(APPLE AND CAUDIO_BUILD_CLI AND CAUDIO_BUNDLE_FFMPEG_MACOS)
  get_filename_component(_ff_libdir "${FFmpeg_AVCODEC_LIBRARY}" DIRECTORY)
  set(_ff_dylibs "")
  foreach(_lib avcodec avformat avutil swresample)
    # Bundle versioned files AND SONAME links: the loader asks for the
    # SONAME by exact name (only the bare dev link stays out, via the glob).
    file(GLOB _cand "${_ff_libdir}/lib${_lib}.*.dylib")
    list(APPEND _ff_dylibs ${_cand})
  endforeach()
  list(REMOVE_DUPLICATES _ff_dylibs)
  if(NOT _ff_dylibs)
    message(WARNING "CAUDIO_BUNDLE_FFMPEG_MACOS requested but no versioned FFmpeg dylibs near ${FFmpeg_AVCODEC_LIBRARY}")
  else()
    install(FILES ${_ff_dylibs} DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime)
    # Same target-property RPATH ordering as the Linux block.
    get_target_property(_cli_rpath caudio_cli INSTALL_RPATH)
    if(_cli_rpath STREQUAL "_cli_rpath-NOTFOUND")
      set(_cli_rpath "")
    endif()
    list(APPEND _cli_rpath "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
    set_target_properties(caudio_cli PROPERTIES INSTALL_RPATH "${_cli_rpath}")
    set(_ff_dylib_names "")
    foreach(_f IN LISTS _ff_dylibs)
      get_filename_component(_n "${_f}" NAME)
      list(APPEND _ff_dylib_names "${_n}")
    endforeach()
    # Exe file name resolved at configure time: genexes are NOT evaluated
    # inside install(CODE), and otool errors here are swallowed by ERROR_QUIET.
    get_target_property(_cli_out caudio_cli OUTPUT_NAME)
    if(_cli_out STREQUAL "_cli_out-NOTFOUND")
      set(_cli_out caudio_cli)
    endif()
    install(CODE "
      set(_exe \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/${_cli_out}\")
      set(_libdir \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}\")
      set(_bins \"\${_exe}\")
      foreach(_n ${_ff_dylib_names})
        list(APPEND _bins \"\${_libdir}/\${_n}\")
        execute_process(COMMAND install_name_tool -id \"@rpath/\${_n}\" \"\${_libdir}/\${_n}\" RESULT_VARIABLE _rc_id OUTPUT_QUIET ERROR_QUIET)
        if(NOT _rc_id EQUAL 0)
          message(FATAL_ERROR \"FFmpeg bundle: install_name_tool -id failed for \${_n}\")
        endif()
      endforeach()
      foreach(_b IN LISTS _bins)
        # Links share their target's load commands (already handled via the
        # real file): processing both would re-change @rpath refs onto
        # themselves and re-sign twice.
        if(IS_SYMLINK \"${_b}\")
          continue()
        endif()
        execute_process(COMMAND otool -L \"\${_b}\" OUTPUT_VARIABLE _refs OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _rc_ot ERROR_QUIET)
        if(NOT _rc_ot EQUAL 0)
          message(FATAL_ERROR \"FFmpeg bundle: otool failed for \${_b}\")
        endif()
        foreach(_n ${_ff_dylib_names})
          # _old keeps the FULL old path: -change matches load commands
          # exactly, and a bare filename silently no-ops.
          string(REGEX MATCH \"[^ \\t\\r\\n]*/\${_n}( |\$)\" _hit \"\${_refs}\")
          if(_hit)
            string(STRIP \"\${_hit}\" _old)
            execute_process(COMMAND install_name_tool -change \"\${_old}\" \"@rpath/\${_n}\" \"\${_b}\" RESULT_VARIABLE _rc_ch OUTPUT_QUIET ERROR_QUIET)
            if(NOT _rc_ch EQUAL 0)
              message(FATAL_ERROR \"FFmpeg bundle: install_name_tool -change failed: \${_old} in \${_b}\")
            endif()
          endif()
        endforeach()
        execute_process(COMMAND codesign --force -s - \"\${_b}\" RESULT_VARIABLE _rc_cs OUTPUT_QUIET ERROR_QUIET)
        if(NOT _rc_cs EQUAL 0)
          message(FATAL_ERROR \"FFmpeg bundle: ad-hoc codesign failed for \${_b}\")
        endif()
      endforeach()
    " COMPONENT runtime)
  endif()
endif()

# C++23 module sources for BMI rebuild (shipped always, built on demand).
# Installed at <prefix>/modules/ (same level as include/). These are pure
# sources: downstream users compile them against the installed headers.
# BMIs are never shipped (compiler/flag-specific); building them stays
# opt-in via CAUDIO_ENABLE_MODULES. In modules mode the component file sets
# above install their own .cppm files and the umbrella is installed explicitly;
# otherwise one directory rule ships them, so no source comes from two rules.
if(CAUDIO_ENABLE_MODULES)
  install(FILES modules/caudio.cppm DESTINATION modules COMPONENT dev)
else()
  install(DIRECTORY modules/ DESTINATION modules COMPONENT dev FILES_MATCHING PATTERN "*.cppm")
endif()

# Man page -- roff source tracked at docs/man/caudio.1
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/man/caudio.1")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/man/caudio.1"
    DESTINATION "${CMAKE_INSTALL_MANDIR}/man1" COMPONENT runtime)
endif()

# License at the archive top level (CPACK_RESOURCE_FILE_LICENSE is not
# bundled into plain TGZ/ZIP archives).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" DESTINATION . COMPONENT runtime)
endif()

# Systemd user service
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/caudio.service")
  install(FILES packaging/caudio.service DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/systemd/user COMPONENT runtime)
endif()

# Packaging (CPack) -- TGZ/ZIP only until the first release; distro
# generators (DEB/RPM/NSIS/DragNDrop) return when versions stabilize.
# Always invoke packaging through the `package` target
# (`cmake --build <build-dir> --target package`): it runs CPack with CWD in
# the build tree. A bare `cpack --config` from the source root stages
# packages under that root instead.
# Components (runtime vs dev, set on each install() above) produce one
# archive per component; the release workflow publishes one format per OS.
set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
# Only our components ship: third-party FetchContent install rules carry no
# COMPONENT and would otherwise land in "Unspecified" archives (TagLib
# headers, .pc/config scripts). Our own tag.lib still ships via the dev
# component (CAUDIO_INSTALL_LIBS); only their loose files are excluded.
set(CPACK_COMPONENTS_ALL runtime dev)
set(CPACK_PACKAGE_NAME "caudio")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "caudio")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++23 music player library -- utils, player, db, engine, ipc, client, service")
set(CPACK_PACKAGE_CONTACT "caudio@example.com")
string(TOLOWER "${CMAKE_SYSTEM_NAME}" CPACK_SYSTEM_NAME)
# Architecture tag for filenames (normalize generator spellings).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64)$")
  set(CAUDIO_ARCH_TAG "x86_64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$")
  set(CAUDIO_ARCH_TAG "arm64")
else()
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" CAUDIO_ARCH_TAG)
endif()
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}${CAUDIO_VERSION_SUFFIX}-${CPACK_SYSTEM_NAME}-${CAUDIO_ARCH_TAG}")
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
endif()
include(CPack)

# Examples (C parity: mini + player_db_demo + engine_demo)
if(CAUDIO_ENABLE_EXAMPLES)
  add_executable(caudio_mini examples/mini_cpp.cpp)
  target_link_libraries(caudio_mini PRIVATE caudio::player caudio::utils)
  target_include_directories(caudio_mini SYSTEM PRIVATE vendor)
  target_compile_features(caudio_mini PRIVATE cxx_std_23)
  caudio_apply_extra_flags(caudio_mini)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(caudio_mini)
  endif()

  add_executable(caudio_player_db_demo examples/player_db_demo.cpp)
  target_link_libraries(caudio_player_db_demo PRIVATE caudio::player caudio::db caudio::utils)
  target_include_directories(caudio_player_db_demo SYSTEM PRIVATE vendor)
  target_compile_features(caudio_player_db_demo PRIVATE cxx_std_23)
  caudio_apply_extra_flags(caudio_player_db_demo)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(caudio_player_db_demo)
  endif()

  add_executable(caudio_engine_demo examples/engine_demo.cpp)
  target_link_libraries(caudio_engine_demo PRIVATE caudio::engine caudio::db caudio::player caudio::utils)
  target_include_directories(caudio_engine_demo SYSTEM PRIVATE vendor)
  target_compile_features(caudio_engine_demo PRIVATE cxx_std_23)
  caudio_apply_extra_flags(caudio_engine_demo)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(caudio_engine_demo)
  endif()
endif()

# Modules-only consumer check: same program as mini_cpp, but importing
# modules instead of including headers. Modules build only.
if(CAUDIO_ENABLE_EXAMPLES AND CAUDIO_ENABLE_MODULES)
  add_executable(caudio_mini_mod examples/mini_mod.cpp)
  target_link_libraries(caudio_mini_mod PRIVATE caudio::player caudio::utils)
  target_compile_features(caudio_mini_mod PRIVATE cxx_std_23)
  caudio_apply_extra_flags(caudio_mini_mod)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(caudio_mini_mod)
  endif()
endif()

# Tests -- Catch2 via system package or FetchContent (144 tests: utils/player/db/engine/ipc/cli paths)
if(CAUDIO_ENABLE_TESTS)
  find_package(Catch2 QUIET)
  if(NOT Catch2_FOUND)
    # Keep Catch2 out of our install/packages (cf. JSON_Install=OFF above).
    set(CATCH_INSTALL_DOCS OFF)
    set(CATCH_INSTALL_EXTRAS OFF)
    FetchContent_Declare(
      Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG v3.16.0
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
  else()
    message(STATUS "Catch2 found (system): tests link Catch2::Catch2WithMain")
  endif()
  include(CTest)
  include(Catch)
  enable_testing()

  if(CAUDIO_TEST_NOAUDIO)
    add_compile_definitions(CAUDIO_TEST_NOAUDIO=1)
  endif()

  function(caudio_add_catch_test name src)
    cmake_parse_arguments(PARSE_ARGV 2 ARG "WITH_TEST_DATA;WITH_ALLOW_MULTIPLE_DEFINITION" "" "LIBS")
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE ${ARG_LIBS} Catch2::Catch2WithMain)
    # + src/: white-box tests cover src-internal helpers (engine detail fns)
    # directly. Build-tree only -- src/ is never installed.
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_include_directories(${name} SYSTEM PRIVATE vendor)
    target_compile_features(${name} PRIVATE cxx_std_23)
    caudio_apply_extra_flags(${name})
    if(ARG_WITH_TEST_DATA)
      target_compile_definitions(${name} PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    endif()
    if(ARG_WITH_ALLOW_MULTIPLE_DEFINITION)
      target_link_options(${name} PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU>,$<NOT:$<PLATFORM_ID:Darwin>>>:-Wl,--allow-multiple-definition>)
    endif()
    if(CAUDIO_ENABLE_SANITIZERS)
      caudio_apply_sanitizers(${name})
    endif()
    catch_discover_tests(${name} PROPERTIES SKIP_RETURN_CODE 4)
  endfunction()

  caudio_add_catch_test(test_utils_result tests/test_utils_result.cpp LIBS caudio::utils)
  caudio_add_catch_test(test_utils_ring tests/test_utils_ring.cpp LIBS caudio::utils)
  caudio_add_catch_test(test_utils_queue tests/test_utils_queue.cpp LIBS caudio::utils)
  caudio_add_catch_test(test_utils_thread tests/test_utils_thread.cpp LIBS caudio::utils)
  caudio_add_catch_test(test_utils_log tests/test_utils_log.cpp LIBS caudio::utils)

  caudio_add_catch_test(test_reader tests/test_reader.cpp LIBS caudio::player caudio::utils)
  caudio_add_catch_test(test_decoder tests/test_decoder.cpp LIBS caudio::player caudio::utils WITH_TEST_DATA)
  caudio_add_catch_test(test_ffmpeg tests/test_ffmpeg.cpp LIBS caudio::player caudio::utils WITH_TEST_DATA)

  caudio_add_catch_test(test_db tests/test_db.cpp LIBS caudio::db caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_write_thread tests/test_write_thread.cpp LIBS caudio::db caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_output tests/test_output.cpp LIBS caudio::player caudio::utils)

  caudio_add_catch_test(test_engine_queue tests/test_engine_queue.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_engine_history tests/test_engine_history.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_engine_events tests/test_engine_events.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_engine_state tests/test_engine_state.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_engine_gapless tests/test_engine_gapless.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio::sqlite3)

  caudio_add_catch_test(test_db_scan tests/test_db_scan.cpp LIBS caudio::db caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_tag_write tests/test_tag_write.cpp LIBS caudio::service caudio::db caudio::player caudio::utils caudio::sqlite3 WITH_TEST_DATA)
  caudio_add_catch_test(test_db_search tests/test_db_search.cpp LIBS caudio::db caudio::utils caudio::sqlite3)
  caudio_add_catch_test(test_db_json tests/test_db_json.cpp LIBS caudio::db caudio::utils caudio::sqlite3)

  caudio_add_catch_test(test_player_integration tests/test_player_integration.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio::sqlite3 WITH_TEST_DATA)

  caudio_add_catch_test(test_ipc tests/test_ipc.cpp LIBS caudio::service caudio::client caudio::ipc caudio::engine caudio::db caudio::player caudio::utils caudio::sqlite3 WITH_ALLOW_MULTIPLE_DEFINITION)
  caudio_add_catch_test(test_config tests/test_config.cpp LIBS caudio::config caudio::utils)

  # CLI golden tests: spawn the real executable with hermetic temp configs
  # and assert on stdout/stderr/exit codes (help texts, parse errors,
  # no-daemon errors, full daemon lifecycle). Needs the CLI binary, so it
  # only exists when CAUDIO_BUILD_CLI is on.
  if(TARGET caudio_cli)
    add_executable(test_cli_golden tests/test_cli_golden.cpp)
    target_link_libraries(test_cli_golden PRIVATE Catch2::Catch2WithMain)
    target_include_directories(test_cli_golden PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_include_directories(test_cli_golden SYSTEM PRIVATE vendor)
    target_compile_features(test_cli_golden PRIVATE cxx_std_23)
    caudio_apply_extra_flags(test_cli_golden)
    target_compile_definitions(test_cli_golden PRIVATE TEST_CLI_BIN="$<TARGET_FILE:caudio_cli>")
    target_compile_definitions(test_cli_golden PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    if(CAUDIO_ENABLE_SANITIZERS)
      caudio_apply_sanitizers(test_cli_golden)
    endif()
    catch_discover_tests(test_cli_golden PROPERTIES SKIP_RETURN_CODE 4)
  endif()

  # D-4: compile every public header as a standalone TU (build gate, no ctest
  # entry -- the objects are never linked, only compiled).
  caudio_add_header_self_containment()
endif()
