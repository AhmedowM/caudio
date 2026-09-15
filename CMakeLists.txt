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

project(caudio VERSION ${PROJECT_VERSION} LANGUAGES CXX)

enable_language(C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
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
  ${CMAKE_CURRENT_BINARY_DIR}/include/caudio/version.hpp
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

# Umbrella module
add_library(caudio_umbrella STATIC src/caudio.cppm)
target_sources(caudio_umbrella PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES src/caudio.cppm)
target_include_directories(caudio_umbrella PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(caudio_umbrella PUBLIC caudio::utils caudio::player caudio::db caudio::engine)
ca_set_warnings(caudio_umbrella)
ca_set_module_warnings(caudio_umbrella)

# caudio executable — App dispatch (CLI11) + main entry
add_executable(caudio
  cli/src/main.cpp
)
target_sources(caudio PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES cli/src/app/app.cppm cli/src/app/core.cppm)
target_include_directories(caudio PRIVATE cli/src/app vendor ${nlohmann_json_SOURCE_DIR}/include ${CLI11_SOURCE_DIR}/include)
target_include_directories(caudio PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(caudio PRIVATE caudio::service caudio::client caudio::cli_shared CLI11::CLI11 Threads::Threads)
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
    target_link_libraries(cli_shared PRIVATE stdc++exp)
  endif()
endif()
if(CAUDIO_ENABLE_SANITIZERS)
  caudio_apply_sanitizers(caudio)
endif()

# ── Documentation (Doxygen) ─────────────────────────────────────────────
# How to build docs:
#   cmake -B build -G Ninja -DCAUDIO_BUILD_DOCS=ON
#   cmake --build build --target doc   # generates HTML to build/docs/html (and docs/html if OUTPUT_DIRECTORY=.)
# Requires Doxygen (optional). If not found, configure still succeeds.
if(CAUDIO_BUILD_DOCS)
  find_package(Doxygen OPTIONAL_COMPONENTS dot)
  if(DOXYGEN_FOUND)
    if(DOXYGEN_DOT_FOUND)
      set(DOXYGEN_HAVE_DOT YES)
    else()
      set(DOXYGEN_HAVE_DOT NO)
    endif()

    # Prefer template docs/Doxyfile.in (CMake @VAR@ substitution) if present
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in")
      configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        @ONLY
      )
      set(_caudio_doxyfile "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in")
      configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        @ONLY
      )
      set(_caudio_doxyfile "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile")
      set(_caudio_doxyfile "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile")
      set(_caudio_doxyfile "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile")
    else()
      message(WARNING "CAUDIO_BUILD_DOCS=ON but no Doxyfile or Doxyfile.in found")
      set(_caudio_doxyfile "")
    endif()

    if(_caudio_doxyfile)
      if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
        doxygen_add_docs(doc CONFIG_FILE "${_caudio_doxyfile}" COMMENT "Generating Doxygen docs")
        # Doxygen on Windows does not auto-create intermediate dirs for HTML_OUTPUT=docs/html
        # Ensure parent exists before doxygen runs (PRE_BUILD works for Ninja/Make)
        if(TARGET doc)
          # Create a helper target to ensure docs/html exists; doc will depend on it
          # Doxygen on Windows does not auto-create intermediate dirs for nested HTML_OUTPUT
          add_custom_target(doc_mkdir
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/docs/html"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/docs/html"
            COMMENT "Preparing docs output directory"
            VERBATIM
          )
          add_dependencies(doc doc_mkdir)
        endif()
      else()
        # Fallback for CMake < 3.16
        add_custom_target(doc
          COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/docs/html"
          COMMAND ${DOXYGEN_EXECUTABLE} "${_caudio_doxyfile}"
          WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
          COMMENT "Generating Doxygen docs (custom target)"
          VERBATIM
        )
      endif()
    endif()
  else()
    message(STATUS "CAUDIO_BUILD_DOCS=ON but Doxygen not found - docs target will not be available (install Doxygen to build docs)")
  endif()
endif()
# ── End Documentation ───────────────────────────────────────────────────

# Install / Export / Packaging like C CMakeLists.txt:334-382
include(GNUInstallDirs)
install(
  TARGETS
    utils utils_shared
    player player_shared
    db db_shared
    engine engine_shared
    json json_shared
    cli_shared
    client client_shared
    service service_shared
    combined
    caudio_sqlite
    blake3
  EXPORT caudioTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/caudio
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/caudio/version.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/caudio)
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

# C++20 module sources for BMI rebuild (consumers need *.cppm to rebuild BMIs)
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/caudio/src FILES_MATCHING PATTERN "*.cppm")
install(DIRECTORY cli/src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/caudio/cli FILES_MATCHING PATTERN "*.cppm")

# Man page — roff source tracked at docs/man/caudio.1
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/man/caudio.1")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/man/caudio.1"
    DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
endif()

# Systemd user service
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/caudio.service")
  install(FILES packaging/caudio.service DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/systemd/user)
endif()

# Packaging (CPack)
set(CPACK_GENERATOR "TGZ;ZIP")
if(UNIX AND NOT APPLE)
  list(APPEND CPACK_GENERATOR "DEB;RPM")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libavcodec-extra | libavcodec59, libavformat59, libavutil57, libswresample4")
  set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
  set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
  set(CPACK_RPM_PACKAGE_REQUIRES "ffmpeg-libs >= 6.0")
  set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
endif()
if(WIN32)
  list(APPEND CPACK_GENERATOR "NSIS;ZIP")
  set(CPACK_NSIS_DISPLAY_NAME "caudio")
endif()
if(APPLE)
  list(APPEND CPACK_GENERATOR "DragNDrop")
endif()
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
  target_compile_features(caudio_mini PRIVATE cxx_std_23)

  add_executable(caudio_player_db_demo examples/player_db_demo.cpp)
  target_link_libraries(caudio_player_db_demo PRIVATE caudio::player caudio::db caudio::utils)
  target_compile_features(caudio_player_db_demo PRIVATE cxx_std_23)

  add_executable(caudio_engine_demo examples/engine_demo.cpp)
  target_link_libraries(caudio_engine_demo PRIVATE caudio::engine caudio::db caudio::player caudio::utils)
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

  function(caudio_add_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE caudio::utils Catch2::Catch2WithMain)
    target_compile_features(${name} PRIVATE cxx_std_23)
    if(CAUDIO_ENABLE_SANITIZERS)
      caudio_apply_sanitizers(${name})
    endif()
    catch_discover_tests(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 4)
  endfunction()

caudio_add_test(test_utils_result tests/test_utils_result.cpp)
caudio_add_test(test_utils_ring tests/test_utils_ring.cpp)
caudio_add_test(test_utils_queue tests/test_utils_queue.cpp)
caudio_add_test(test_utils_thread tests/test_utils_thread.cpp)
caudio_add_test(test_utils_log tests/test_utils_log.cpp)

  # Task 3: Reader + DecoderRegistry
  function(caudio_add_player_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE caudio::player caudio::utils Catch2::Catch2WithMain)
    target_compile_features(${name} PRIVATE cxx_std_23)
    if(${name} STREQUAL "test_decoder")
      target_compile_definitions(${name} PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    endif()
    if(CAUDIO_ENABLE_SANITIZERS)
      caudio_apply_sanitizers(${name})
    endif()
    catch_discover_tests(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 4)
  endfunction()
  caudio_add_player_test(test_reader tests/test_reader.cpp)
  caudio_add_player_test(test_decoder tests/test_decoder.cpp)
  caudio_add_player_test(test_ffmpeg tests/test_ffmpeg.cpp)
  target_compile_definitions(test_ffmpeg PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

  # DB tests
  add_executable(test_db tests/test_db.cpp)
  target_link_libraries(test_db PRIVATE caudio::db caudio::utils Catch2::Catch2WithMain caudio_sqlite)
  target_compile_features(test_db PRIVATE cxx_std_23)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(test_db)
  endif()
  catch_discover_tests(test_db)
  add_test(NAME test_db COMMAND test_db)
  set_tests_properties(test_db PROPERTIES SKIP_RETURN_CODE 4)

  # Write thread tests
  add_executable(test_write_thread tests/test_write_thread.cpp)
  target_link_libraries(test_write_thread PRIVATE caudio::db caudio::utils Catch2::Catch2WithMain caudio_sqlite)
  target_include_directories(test_write_thread PRIVATE vendor)
  target_compile_features(test_write_thread PRIVATE cxx_std_23)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(test_write_thread)
  endif()
  catch_discover_tests(test_write_thread)
  add_test(NAME test_write_thread COMMAND test_write_thread)
  set_tests_properties(test_write_thread PROPERTIES SKIP_RETURN_CODE 4)

  # Output tests
  add_executable(test_output tests/test_output.cpp)
  target_link_libraries(test_output PRIVATE caudio::player caudio::utils Catch2::Catch2WithMain)
  target_compile_features(test_output PRIVATE cxx_std_23)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(test_output)
  endif()
  catch_discover_tests(test_output)
  add_test(NAME test_output COMMAND test_output)
  set_tests_properties(test_output PROPERTIES SKIP_RETURN_CODE 4)

  # Task 11: Engine parity
  function(caudio_add_engine_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE caudio::engine caudio::db caudio::player caudio::utils Catch2::Catch2WithMain caudio_sqlite)
    target_include_directories(${name} PRIVATE vendor)
    target_compile_features(${name} PRIVATE cxx_std_23)
    if(CAUDIO_ENABLE_SANITIZERS)
      caudio_apply_sanitizers(${name})
    endif()
    catch_discover_tests(${name})
  endfunction()
  caudio_add_engine_test(test_engine_queue tests/test_engine_queue.cpp)
  caudio_add_engine_test(test_engine_history tests/test_engine_history.cpp)
  caudio_add_engine_test(test_engine_events tests/test_engine_events.cpp)
  caudio_add_engine_test(test_engine_state tests/test_engine_state.cpp)
  caudio_add_engine_test(test_engine_gapless tests/test_engine_gapless.cpp)

  # DB scan/search/json
  add_executable(test_db_scan tests/test_db_scan.cpp)
  target_link_libraries(test_db_scan PRIVATE caudio::db caudio::utils Catch2::Catch2WithMain caudio_sqlite)
  target_include_directories(test_db_scan PRIVATE vendor)
  target_compile_features(test_db_scan PRIVATE cxx_std_23)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(test_db_scan)
  endif()
  catch_discover_tests(test_db_scan)

  add_executable(test_db_search tests/test_db_search.cpp)
  target_link_libraries(test_db_search PRIVATE caudio::db caudio::utils Catch2::Catch2WithMain caudio_sqlite)
  target_include_directories(test_db_search PRIVATE vendor)
  target_compile_features(test_db_search PRIVATE cxx_std_23)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(test_db_search)
  endif()
  catch_discover_tests(test_db_search)

  add_executable(test_db_json tests/test_db_json.cpp)
  target_link_libraries(test_db_json PRIVATE caudio::db caudio::utils Catch2::Catch2WithMain caudio_sqlite)
  target_include_directories(test_db_json PRIVATE vendor ${nlohmann_json_SOURCE_DIR}/include)
  target_compile_features(test_db_json PRIVATE cxx_std_23)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(test_db_json)
  endif()
  catch_discover_tests(test_db_json)

  # Player integration
  add_executable(test_player_integration tests/test_player_integration.cpp)
  target_link_libraries(test_player_integration PRIVATE caudio::engine caudio::db caudio::player caudio::utils Catch2::Catch2WithMain caudio_sqlite)
  target_include_directories(test_player_integration PRIVATE vendor)
  target_compile_features(test_player_integration PRIVATE cxx_std_23)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(test_player_integration)
  endif()
  catch_discover_tests(test_player_integration)

  # IPC/Cli integration tests
  add_executable(test_ipc tests/test_ipc.cpp)
  target_link_libraries(test_ipc PRIVATE caudio::service caudio::client caudio::cli_shared caudio::engine caudio::db caudio::player caudio::utils Catch2::Catch2WithMain caudio_sqlite)
  target_include_directories(test_ipc PRIVATE vendor ${nlohmann_json_SOURCE_DIR}/include)
  target_compile_features(test_ipc PRIVATE cxx_std_23)
  target_link_options(test_ipc PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)
  if(CAUDIO_ENABLE_SANITIZERS)
    caudio_apply_sanitizers(test_ipc)
  endif()
  catch_discover_tests(test_ipc)
endif()
