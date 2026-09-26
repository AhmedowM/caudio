cmake_minimum_required(VERSION 3.28)

# Get version from git tags (e.g., v0.4.0 -> 0.4.0) — same workflow as C CMakeLists.txt:4-35
execute_process(
  COMMAND git describe --tags --abbrev=0
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_TAG
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(GIT_TAG MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")
  set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")
  set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}")
  set(PROJECT_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
else()
  set(PROJECT_VERSION_MAJOR "0")
  set(PROJECT_VERSION_MINOR "1")
  set(PROJECT_VERSION_PATCH "0")
  set(PROJECT_VERSION "0.1.0")
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

# Options — FFmpeg is required primary (dr_* deleted, vendor cleaned)
option(CAUDIO_WITH_FETCH_FFMPEG "auto-fetch FFmpeg if not found (system → vcpkg/Conan → prebuilt → source)" ON)
option(CAUDIO_ENABLE_TESTS "build tests" OFF)
option(CAUDIO_ENABLE_EXAMPLES "build examples" OFF)
option(CAUDIO_ENABLE_SANITIZERS "Enable ASan+UBSan (like C: -fsanitize=address,undefined)" OFF)
option(CAUDIO_BUILD_DOCS "Build Doxygen documentation" OFF)
option(CAUDIO_TEST_NOAUDIO "skip audio device tests (no beep) for headless CI" OFF)
option(CAUDIO_ENABLE_CLANG_TIDY "enable clang-tidy checks during build" OFF)
# NOTE: ccache is environment configuration, not a project option; pass
# -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache instead.

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

find_package(FFmpeg QUIET)
if(FFmpeg_FOUND)
  message(STATUS "FFmpeg found (system): ${FFmpeg_AVCODEC_LIBRARY}")
elseif(CAUDIO_WITH_FETCH_FFMPEG)
  include(cmake/SetupFFmpeg.cmake)
  caudio_setup_ffmpeg()
  if(NOT FFmpeg_FOUND)
    message(FATAL_ERROR "FFmpeg is required — install 9.0.1+ or set FFmpeg_ROOT")
  endif()
  message(STATUS "FFmpeg found via fallback: ${FFmpeg_AVCODEC_LIBRARY}")
else()
  message(FATAL_ERROR "FFmpeg is required — install 9.0.1+ or set FFmpeg_ROOT")
endif()

function(caudio_apply_sanitizers tgt)
  if(WIN32)
    return()
  elseif(MSVC)
    target_compile_options(${tgt} PRIVATE /fsanitize=address)
    target_link_options(${tgt} PRIVATE /fsanitize=address)
  else()
    target_compile_options(${tgt} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    target_link_options(${tgt} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

if(CAUDIO_ENABLE_SANITIZERS)
  if(WIN32)
    message(WARNING "CAUDIO_ENABLE_SANITIZERS not supported on Windows/MinGW — sanitizers require Clang/GCC on Linux. Option ignored.")
  elseif(MSVC)
    message(WARNING "CAUDIO_ENABLE_SANITIZERS: MSVC sanitizers limited; using /fsanitize=address")
  else()
    message(STATUS "Sanitizers enabled: ASan+UBSan")
  endif()
endif()

# Configure version header (template lives in cmake/ — canonical for CMake @ONLY files)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/caudio/version_config.hpp
  @ONLY
)

# Vendor: caudio_sqlite STATIC (SQLITE_ENABLE_FTS5=1)
add_library(caudio_sqlite STATIC vendor/sqlite3.c)
set_target_properties(caudio_sqlite PROPERTIES OUTPUT_NAME caudio_sqlite)
target_include_directories(caudio_sqlite PRIVATE vendor)
target_include_directories(caudio_sqlite PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_definitions(caudio_sqlite PUBLIC SQLITE_ENABLE_FTS5=1)

# Vendor: BLAKE3 C core (portable only - no AVX/SSE/NEON)
add_library(blake3 STATIC vendor/blake3.c vendor/blake3_portable.c)
target_include_directories(blake3 PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_definitions(blake3 PRIVATE BLAKE3_NO_AVX512 BLAKE3_NO_AVX2 BLAKE3_NO_SSE41 BLAKE3_NO_SSE2 BLAKE3_USE_NEON=0)
# suppress GCC warnings in vendored C
if(NOT MSVC)
  target_compile_options(blake3 PRIVATE -w)
endif()

# FetchContent deps needed before components (nlohmann_json_SOURCE_DIR etc.)
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(CLI11)

# DRY helpers + component split (include preserves single-BMI build)
include(cmake/CaudioHelpers.cmake)
include(cmake/components/utils.cmake)
include(cmake/components/player.cmake)
include(cmake/components/db.cmake)
include(cmake/components/engine.cmake)
include(cmake/components/cli.cmake)
include(cmake/components/combined.cmake)

# Umbrella module (optional, headers are canonical)
if(CAUDIO_ENABLE_MODULES)
  add_library(caudio_umbrella STATIC src/caudio.cppm)
  target_sources(caudio_umbrella PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES src/caudio.cppm)
  target_include_directories(caudio_umbrella PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
  target_link_libraries(caudio_umbrella PUBLIC caudio::utils caudio::player caudio::db caudio::engine)
  ca_set_warnings(caudio_umbrella)
  ca_set_module_warnings(caudio_umbrella)
endif()

# caudio executable — App dispatch (CLI11) + main entry (header-source only)
add_executable(caudio
  cli/src/main.cpp
)
target_sources(caudio PRIVATE cli/src/app/core.cpp)
if(CAUDIO_ENABLE_MODULES)
  target_sources(caudio PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES cli/src/app/app.cppm cli/src/app/core.cppm)
endif()
target_include_directories(caudio PRIVATE cli/src/app vendor ${nlohmann_json_SOURCE_DIR}/include ${CLI11_SOURCE_DIR}/include)
target_include_directories(caudio PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(caudio PRIVATE caudio::service caudio::client caudio::ipc CLI11::CLI11 Threads::Threads)
target_link_libraries(caudio PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
ca_set_warnings(caudio)
ca_set_module_warnings(caudio)
# Needed on MinGW for template instantiation deduplication across executable + static libs
target_link_options(caudio PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)
# GCC 14+ <print> requires libstdc++exp on MinGW/GCC
if(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  find_library(STDCXX_EXP_LIB stdc++exp)
  if(STDCXX_EXP_LIB)
    target_link_libraries(caudio PRIVATE stdc++exp)
    target_link_libraries(client PRIVATE stdc++exp)
    target_link_libraries(ipc PRIVATE stdc++exp)
  endif()
endif()
if(CAUDIO_ENABLE_SANITIZERS)
  caudio_apply_sanitizers(caudio)
endif()

# Documentation (Doxygen) — see cmake/Docs.cmake
if(CAUDIO_BUILD_DOCS)
  include(cmake/Docs.cmake)
  caudio_add_docs()
endif()

# Install / Export / Packaging like C CMakeLists.txt:334-382
include(GNUInstallDirs)
set(CAUDIO_INSTALL_LIBS
  utils utils_shared
  player player_shared
  db db_shared
  engine engine_shared
  ipc
  client client_shared
  service service_shared
  combined
  caudio_sqlite
  blake3
)
if(CAUDIO_ENABLE_MODULES)
  install(
    TARGETS ${CAUDIO_INSTALL_LIBS}
    EXPORT caudioTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/caudio
  )
else()
  install(
    TARGETS ${CAUDIO_INSTALL_LIBS}
    EXPORT caudioTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/caudio/version_config.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/caudio)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES cmake/FindFFmpeg.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caudio)
install(EXPORT caudioTargets NAMESPACE caudio:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caudio)

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
)

install(TARGETS caudio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# C++23 module sources for BMI rebuild (only when modules enabled)
if(CAUDIO_ENABLE_MODULES)
  install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/caudio/src FILES_MATCHING PATTERN "*.cppm")
  install(DIRECTORY cli/src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/caudio/cli FILES_MATCHING PATTERN "*.cppm")
endif()

# Man page — roff source tracked at docs/man/caudio.1
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/man/caudio.1")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/man/caudio.1"
    DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
endif()

# Systemd user service
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/caudio.service")
  install(FILES packaging/caudio.service DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/systemd/user)
endif()

# Packaging (CPack) — TGZ/ZIP only until the first release; distro
# generators (DEB/RPM/NSIS/DragNDrop) return when versions stabilize.
set(CPACK_GENERATOR "TGZ;ZIP")
set(CPACK_PACKAGE_NAME "caudio")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "caudio")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++23 music player library — utils, player, db, engine")
set(CPACK_PACKAGE_CONTACT "caudio@example.com")
string(TOLOWER "${CMAKE_SYSTEM_NAME}" CPACK_SYSTEM_NAME)
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}")
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
endif()
include(CPack)

# Examples (C parity: mini + player_db_demo + engine_demo)
if(CAUDIO_ENABLE_EXAMPLES)
  add_executable(caudio_mini examples/mini_cpp.cpp)
  target_link_libraries(caudio_mini PRIVATE caudio::player caudio::utils)
  target_include_directories(caudio_mini PRIVATE vendor)
  target_compile_features(caudio_mini PRIVATE cxx_std_23)

  add_executable(caudio_player_db_demo examples/player_db_demo.cpp)
  target_link_libraries(caudio_player_db_demo PRIVATE caudio::player caudio::db caudio::utils)
  target_include_directories(caudio_player_db_demo PRIVATE vendor ${nlohmann_json_SOURCE_DIR}/include)
  target_compile_features(caudio_player_db_demo PRIVATE cxx_std_23)

  add_executable(caudio_engine_demo examples/engine_demo.cpp)
  target_link_libraries(caudio_engine_demo PRIVATE caudio::engine caudio::db caudio::player caudio::utils)
  target_include_directories(caudio_engine_demo PRIVATE vendor ${nlohmann_json_SOURCE_DIR}/include)
  target_compile_features(caudio_engine_demo PRIVATE cxx_std_23)
endif()

# Tests — Catch2 via FetchContent, ports of test_utils_*
if(CAUDIO_ENABLE_TESTS)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
  )
  FetchContent_MakeAvailable(Catch2)
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
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests vendor ${nlohmann_json_SOURCE_DIR}/include)
    target_compile_features(${name} PRIVATE cxx_std_23)
    if(ARG_WITH_TEST_DATA)
      target_compile_definitions(${name} PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    endif()
    if(ARG_WITH_ALLOW_MULTIPLE_DEFINITION)
      target_link_options(${name} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)
    endif()
    if(CAUDIO_ENABLE_SANITIZERS)
      caudio_apply_sanitizers(${name})
    endif()
    catch_discover_tests(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 4)
  endfunction()

  caudio_add_catch_test(test_utils_result tests/test_utils_result.cpp LIBS caudio::utils)
  caudio_add_catch_test(test_utils_ring tests/test_utils_ring.cpp LIBS caudio::utils)
  caudio_add_catch_test(test_utils_queue tests/test_utils_queue.cpp LIBS caudio::utils)
  caudio_add_catch_test(test_utils_thread tests/test_utils_thread.cpp LIBS caudio::utils)
  caudio_add_catch_test(test_utils_log tests/test_utils_log.cpp LIBS caudio::utils)

  caudio_add_catch_test(test_reader tests/test_reader.cpp LIBS caudio::player caudio::utils)
  caudio_add_catch_test(test_decoder tests/test_decoder.cpp LIBS caudio::player caudio::utils WITH_TEST_DATA)
  caudio_add_catch_test(test_ffmpeg tests/test_ffmpeg.cpp LIBS caudio::player caudio::utils WITH_TEST_DATA)

  caudio_add_catch_test(test_db tests/test_db.cpp LIBS caudio::db caudio::utils caudio_sqlite)
  caudio_add_catch_test(test_write_thread tests/test_write_thread.cpp LIBS caudio::db caudio::utils caudio_sqlite)
  caudio_add_catch_test(test_output tests/test_output.cpp LIBS caudio::player caudio::utils)

  caudio_add_catch_test(test_engine_queue tests/test_engine_queue.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio_sqlite)
  caudio_add_catch_test(test_engine_history tests/test_engine_history.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio_sqlite)
  caudio_add_catch_test(test_engine_events tests/test_engine_events.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio_sqlite)
  caudio_add_catch_test(test_engine_state tests/test_engine_state.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio_sqlite)
  caudio_add_catch_test(test_engine_gapless tests/test_engine_gapless.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio_sqlite)

  caudio_add_catch_test(test_db_scan tests/test_db_scan.cpp LIBS caudio::db caudio::utils caudio_sqlite)
  caudio_add_catch_test(test_db_search tests/test_db_search.cpp LIBS caudio::db caudio::utils caudio_sqlite)
  caudio_add_catch_test(test_db_json tests/test_db_json.cpp LIBS caudio::db caudio::utils caudio_sqlite)

  caudio_add_catch_test(test_player_integration tests/test_player_integration.cpp LIBS caudio::engine caudio::db caudio::player caudio::utils caudio_sqlite)

  caudio_add_catch_test(test_ipc tests/test_ipc.cpp LIBS caudio::service caudio::client caudio::ipc caudio::engine caudio::db caudio::player caudio::utils caudio_sqlite WITH_ALLOW_MULTIPLE_DEFINITION)
endif()
