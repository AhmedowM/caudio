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

# Options — CAUDIO_WITH_FFMPEG ON required primary (C had OFF CMakeLists.txt:62)
option(CAUDIO_WITH_FFMPEG "use FFmpeg as primary decoder" ON)
option(CAUDIO_BUILD_TESTS "build tests" OFF)
option(CAUDIO_BUILD_EXAMPLES "build examples" OFF)
option(CAUDIO_ENABLE_SANITIZERS "Enable ASan+UBSan (like C: -fsanitize=address,undefined)" OFF)

# Module path for FindFFmpeg
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Threads REQUIRED)

option(CAUDIO_FETCH_FFMPEG "auto-fetch FFmpeg if not found (system → vcpkg/Conan → prebuilt → source)" ON)

if(CAUDIO_WITH_FFMPEG)
  find_package(FFmpeg QUIET)
  if(FFmpeg_FOUND)
    message(STATUS "FFmpeg found (system): ${FFmpeg_AVCODEC_LIBRARY}")
  elseif(CAUDIO_FETCH_FFMPEG)
    include(cmake/SetupFFmpeg.cmake)
    caudio_setup_ffmpeg()
    if(FFmpeg_FOUND)
      message(STATUS "FFmpeg found via fallback: ${FFmpeg_AVCODEC_LIBRARY}")
    else()
      message(WARNING "FFmpeg fallback failed — building without FFmpeg, using miniaudio fallback")
    endif()
  else()
    message(STATUS "FFmpeg not found — building without FFmpeg (set -DCAUDIO_FETCH_FFMPEG=ON to auto-fetch)")
  endif()
  if(NOT FFmpeg_FOUND)
    message(STATUS "FFmpeg not found — miniaudio decoder will handle WAV/FLAC/MP3/Vorbis, other formats will use synthetic fallback")
  endif()
endif()

if(CAUDIO_ENABLE_SANITIZERS)
  if(WIN32)
    message(WARNING "CAUDIO_ENABLE_SANITIZERS not supported on Windows/MinGW — sanitizers require Clang/GCC on Linux. Option ignored.")
  elseif(MSVC)
    message(WARNING "CAUDIO_ENABLE_SANITIZERS: MSVC sanitizers limited; using /fsanitize=address")
    add_compile_options(/fsanitize=address)
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    add_link_options(-fsanitize=address,undefined)
    message(STATUS "Sanitizers enabled: ASan+UBSan")
  endif()
endif()

# Configure version header
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/caudio/version.hpp
  @ONLY
)

# Vendor: single caudio_sqlite OBJECT/STATIC (SQLITE_ENABLE_FTS5=1)
add_library(caudio_sqlite OBJECT vendor/sqlite3.c)
target_include_directories(caudio_sqlite PRIVATE vendor)
target_include_directories(caudio_sqlite PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor>)
target_compile_definitions(caudio_sqlite PRIVATE SQLITE_ENABLE_FTS5=1)
set_target_properties(caudio_sqlite PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(caudio_sqlite_static STATIC vendor/sqlite3.c)
set_target_properties(caudio_sqlite_static PROPERTIES OUTPUT_NAME caudio_sqlite)
target_include_directories(caudio_sqlite_static PRIVATE vendor)
target_include_directories(caudio_sqlite_static PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_definitions(caudio_sqlite_static PUBLIC SQLITE_ENABLE_FTS5=1)
target_compile_definitions(caudio_sqlite_static PRIVATE SQLITE_ENABLE_FTS5=1)

# Vendor: RtAudio OBJECT (will be replaced with real RtAudio in Task 5)
add_library(caudio_rtaudio OBJECT vendor/RtAudio.cpp)
target_include_directories(caudio_rtaudio PUBLIC vendor)
set_target_properties(caudio_rtaudio PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Warnings
set(CAUDIO_WARNING_FLAGS
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)

function(ca_set_warnings tgt)
  target_compile_options(${tgt} PRIVATE ${CAUDIO_WARNING_FLAGS})
endfunction()

# caudio::utils — C++23 named module partitioned, no hpp split
set(CAUDIO_UTILS_SOURCES
  src/utils/utils.cppm
  src/utils/result.cppm
  src/utils/error.cppm
  src/utils/log.cppm
  src/utils/arena.cppm
  src/utils/ring.cppm
  src/utils/queue.cppm
  src/utils/thread.cppm
)
add_library(caudio_utils STATIC)
add_library(caudio::utils ALIAS caudio_utils)
target_sources(caudio_utils PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES ${CAUDIO_UTILS_SOURCES})
target_include_directories(caudio_utils PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(caudio_utils PUBLIC Threads::Threads)
ca_set_warnings(caudio_utils)

add_library(caudio_utils_shared SHARED)
set_target_properties(caudio_utils_shared PROPERTIES OUTPUT_NAME caudio_utils)
add_library(caudio::utils_shared ALIAS caudio_utils_shared)
target_sources(caudio_utils_shared PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES ${CAUDIO_UTILS_SOURCES})
target_include_directories(caudio_utils_shared PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(caudio_utils_shared PUBLIC Threads::Threads)
ca_set_warnings(caudio_utils_shared)

# caudio::player — partitioned modules + decoders (Task 3/4)
set(CAUDIO_PLAYER_SOURCES
  src/player/player.cppm
  src/player/reader.cppm
  src/player/output.cppm
  src/player/decoder.cppm
  src/player/decoders/decoder_common.cppm
  src/player/decoders/decoder_interface.cppm
  src/player/decoders/miniaudio_impl.cppm
)
if(CAUDIO_WITH_FFMPEG AND FFmpeg_FOUND)
  list(APPEND CAUDIO_PLAYER_SOURCES src/player/decoders/ffmpeg.cppm)
endif()
add_library(caudio_player STATIC)
add_library(caudio::player ALIAS caudio_player)
target_sources(caudio_player PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES ${CAUDIO_PLAYER_SOURCES})
target_include_directories(caudio_player PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(caudio_player PRIVATE vendor)
target_link_libraries(caudio_player PUBLIC caudio::utils)
if(CAUDIO_WITH_FFMPEG AND FFmpeg_FOUND)
  target_link_libraries(caudio_player PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
  target_compile_definitions(caudio_player PUBLIC CAUDIO_WITH_FFMPEG=1)
endif()
target_sources(caudio_player PRIVATE $<TARGET_OBJECTS:caudio_rtaudio>)
ca_set_warnings(caudio_player)

add_library(caudio_player_shared SHARED)
set_target_properties(caudio_player_shared PROPERTIES OUTPUT_NAME caudio_player)
add_library(caudio::player_shared ALIAS caudio_player_shared)
target_sources(caudio_player_shared PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES ${CAUDIO_PLAYER_SOURCES})
target_include_directories(caudio_player_shared PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(caudio_player_shared PRIVATE vendor)
target_link_libraries(caudio_player_shared PUBLIC caudio::utils_shared)
if(CAUDIO_WITH_FFMPEG AND FFmpeg_FOUND)
  target_link_libraries(caudio_player_shared PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
  target_compile_definitions(caudio_player_shared PUBLIC CAUDIO_WITH_FFMPEG=1)
endif()
target_sources(caudio_player_shared PRIVATE $<TARGET_OBJECTS:caudio_rtaudio>)
ca_set_warnings(caudio_player_shared)

# nlohmann json for ordered_json
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# caudio::db — single caudio_sqlite STATIC, shared_mutex+Transaction later
add_library(caudio_db STATIC)
add_library(caudio::db ALIAS caudio_db)
target_sources(caudio_db PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES src/db/db.cppm
  src/db/types.cppm
  src/db/schema.cppm
  src/db/database.cppm
  src/db/scan.cppm
  src/db/search.cppm
  src/db/json.cppm
  src/db/write_thread.cppm)
target_include_directories(caudio_db PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(caudio_db PRIVATE vendor)
target_link_libraries(caudio_db PUBLIC caudio::utils Threads::Threads)
  target_include_directories(caudio_db PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
target_sources(caudio_db PRIVATE $<TARGET_OBJECTS:caudio_sqlite>)
target_compile_definitions(caudio_db PUBLIC SQLITE_ENABLE_FTS5=1)
ca_set_warnings(caudio_db)

add_library(caudio_db_shared SHARED)
set_target_properties(caudio_db_shared PROPERTIES OUTPUT_NAME caudio_db)
add_library(caudio::db_shared ALIAS caudio_db_shared)
target_sources(caudio_db_shared PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES src/db/db.cppm
  src/db/types.cppm
  src/db/schema.cppm
  src/db/database.cppm
  src/db/scan.cppm
  src/db/search.cppm
  src/db/json.cppm
  src/db/write_thread.cppm)
target_include_directories(caudio_db_shared PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(caudio_db_shared PRIVATE vendor)
target_link_libraries(caudio_db_shared PUBLIC caudio::utils_shared Threads::Threads)
  target_include_directories(caudio_db_shared PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
target_sources(caudio_db_shared PRIVATE $<TARGET_OBJECTS:caudio_sqlite>)
target_compile_definitions(caudio_db_shared PUBLIC SQLITE_ENABLE_FTS5=1)
ca_set_warnings(caudio_db_shared)

# caudio::engine
add_library(caudio_engine STATIC)
add_library(caudio::engine ALIAS caudio_engine)
target_sources(caudio_engine PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES src/engine/engine.cppm)
target_include_directories(caudio_engine PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(caudio_engine PUBLIC caudio::db caudio::player caudio::utils Threads::Threads)
ca_set_warnings(caudio_engine)

add_library(caudio_engine_shared SHARED)
set_target_properties(caudio_engine_shared PROPERTIES OUTPUT_NAME caudio_engine)
add_library(caudio::engine_shared ALIAS caudio_engine_shared)
target_sources(caudio_engine_shared PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES src/engine/engine.cppm)
target_include_directories(caudio_engine_shared PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(caudio_engine_shared PUBLIC caudio::db_shared caudio::player_shared caudio::utils_shared Threads::Threads)
ca_set_warnings(caudio_engine_shared)

# Optional combined shared library: caudio (utils+player+db+engine)
add_library(caudio_combined SHARED)
set_target_properties(caudio_combined PROPERTIES OUTPUT_NAME caudio)
target_sources(caudio_combined PUBLIC
  FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
    ${CAUDIO_UTILS_SOURCES}
    ${CAUDIO_PLAYER_SOURCES}
    src/db/db.cppm
  src/db/types.cppm
  src/db/schema.cppm
  src/db/database.cppm
  src/db/scan.cppm
  src/db/search.cppm
  src/db/json.cppm
  src/db/write_thread.cppm
    src/engine/engine.cppm
)
target_include_directories(caudio_combined PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(caudio_combined PRIVATE vendor)
target_link_libraries(caudio_combined PUBLIC Threads::Threads)
  target_include_directories(caudio_combined PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
target_sources(caudio_combined PRIVATE $<TARGET_OBJECTS:caudio_sqlite> $<TARGET_OBJECTS:caudio_rtaudio>)
target_compile_definitions(caudio_combined PUBLIC SQLITE_ENABLE_FTS5=1)
if(CAUDIO_WITH_FFMPEG AND FFmpeg_FOUND)
  target_link_libraries(caudio_combined PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
  target_compile_definitions(caudio_combined PUBLIC CAUDIO_WITH_FFMPEG=1)
endif()
ca_set_warnings(caudio_combined)

# Install / Export / Packaging like C CMakeLists.txt:334-382
include(GNUInstallDirs)
install(
  TARGETS
    caudio_utils caudio_utils_shared
    caudio_player caudio_player_shared
    caudio_db caudio_db_shared
    caudio_engine caudio_engine_shared
    caudio_combined
    caudio_sqlite_static
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

# Packaging (CPack) — TGZ;ZIP like C
set(CPACK_PACKAGE_NAME "caudio")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "caudio")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++23 music player library — utils, player, db, engine")
set(CPACK_PACKAGE_CONTACT "caudio@example.com")
string(TOLOWER "${CMAKE_SYSTEM_NAME}" CPACK_SYSTEM_NAME)
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}")
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set(CPACK_GENERATOR "TGZ;ZIP")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
endif()
include(CPack)

# Examples (C parity: mini + player_db_demo + engine_demo)
if(CAUDIO_BUILD_EXAMPLES)
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
if(CAUDIO_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
  )
  FetchContent_MakeAvailable(Catch2)
  include(CTest)
  include(Catch)
  enable_testing()

  function(caudio_add_test name src impl)
    add_executable(${name} ${src} ${impl})
    target_link_libraries(${name} PRIVATE caudio::utils Catch2::Catch2WithMain)
    target_compile_features(${name} PRIVATE cxx_std_23)
    catch_discover_tests(${name})
    add_test(NAME ${name} COMMAND ${name})
  endfunction()

  caudio_add_test(test_utils_result tests/test_utils_result.cpp tests/helpers/test_utils_result_impl.cpp)
  caudio_add_test(test_utils_ring tests/test_utils_ring.cpp tests/helpers/test_utils_ring_impl.cpp)
  caudio_add_test(test_utils_queue tests/test_utils_queue.cpp tests/helpers/test_utils_queue_impl.cpp)
  caudio_add_test(test_utils_thread tests/test_utils_thread.cpp tests/helpers/test_utils_thread_impl.cpp)
  caudio_add_test(test_utils_arena tests/test_utils_arena.cpp tests/helpers/test_utils_arena_impl.cpp)
  caudio_add_test(test_utils_log tests/test_utils_log.cpp tests/helpers/test_utils_log_impl.cpp)

  # Task 3: Reader + DecoderRegistry
  function(caudio_add_player_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE caudio::player caudio::utils Catch2::Catch2WithMain)
    target_compile_features(${name} PRIVATE cxx_std_23)
    if(${name} STREQUAL "test_decoder")
      target_compile_definitions(${name} PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    endif()
    catch_discover_tests(${name})
    add_test(NAME ${name} COMMAND ${name})
  endfunction()
  caudio_add_player_test(test_reader tests/test_reader.cpp)
  caudio_add_player_test(test_decoder tests/test_decoder.cpp)
  caudio_add_player_test(test_ffmpeg tests/test_ffmpeg.cpp)
  target_compile_definitions(test_ffmpeg PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

  # DB tests
  add_executable(test_db tests/test_db.cpp tests/helpers/test_db_impl.cpp)
  target_link_libraries(test_db PRIVATE caudio::db caudio::utils Catch2::Catch2WithMain caudio_sqlite_static)
  target_compile_features(test_db PRIVATE cxx_std_23)
  catch_discover_tests(test_db)
  add_test(NAME test_db COMMAND test_db)

  # Write thread tests
  add_executable(test_write_thread tests/test_write_thread.cpp)
  target_link_libraries(test_write_thread PRIVATE caudio::db caudio::utils Catch2::Catch2WithMain caudio_sqlite_static)
  target_include_directories(test_write_thread PRIVATE vendor)
  target_compile_features(test_write_thread PRIVATE cxx_std_23)
  catch_discover_tests(test_write_thread)
  add_test(NAME test_write_thread COMMAND test_write_thread)

  # Output tests
  add_executable(test_output tests/test_output.cpp)
  target_link_libraries(test_output PRIVATE caudio::player caudio::utils Catch2::Catch2WithMain)
  target_compile_features(test_output PRIVATE cxx_std_23)
  catch_discover_tests(test_output)
  add_test(NAME test_output COMMAND test_output)

  # Task 11: Engine parity
  function(caudio_add_engine_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE caudio::engine caudio::db caudio::player caudio::utils Catch2::Catch2WithMain caudio_sqlite_static)
    target_include_directories(${name} PRIVATE vendor)
    target_compile_features(${name} PRIVATE cxx_std_23)
    catch_discover_tests(${name})
  endfunction()
  caudio_add_engine_test(test_engine_queue tests/test_engine_queue.cpp)
  caudio_add_engine_test(test_engine_history tests/test_engine_history.cpp)
  caudio_add_engine_test(test_engine_events tests/test_engine_events.cpp)
  caudio_add_engine_test(test_engine_state tests/test_engine_state.cpp)
  caudio_add_engine_test(test_engine_gapless tests/test_engine_gapless.cpp)

  # DB scan/search/json
  add_executable(test_db_scan tests/test_db_scan.cpp)
  target_link_libraries(test_db_scan PRIVATE caudio::db caudio::utils Catch2::Catch2WithMain caudio_sqlite_static)
  target_include_directories(test_db_scan PRIVATE vendor)
  target_compile_features(test_db_scan PRIVATE cxx_std_23)
  catch_discover_tests(test_db_scan)

  add_executable(test_db_search tests/test_db_search.cpp)
  target_link_libraries(test_db_search PRIVATE caudio::db caudio::utils Catch2::Catch2WithMain caudio_sqlite_static)
  target_include_directories(test_db_search PRIVATE vendor)
  target_compile_features(test_db_search PRIVATE cxx_std_23)
  catch_discover_tests(test_db_search)

  add_executable(test_db_json tests/test_db_json.cpp)
  target_link_libraries(test_db_json PRIVATE caudio::db caudio::utils Catch2::Catch2WithMain caudio_sqlite_static)
  target_include_directories(test_db_json PRIVATE vendor ${nlohmann_json_SOURCE_DIR}/include)
  target_compile_features(test_db_json PRIVATE cxx_std_23)
  catch_discover_tests(test_db_json)

  # Player integration
  add_executable(test_player_integration tests/test_player_integration.cpp)
  target_link_libraries(test_player_integration PRIVATE caudio::engine caudio::db caudio::player caudio::utils Catch2::Catch2WithMain caudio_sqlite_static)
  target_include_directories(test_player_integration PRIVATE vendor)
  target_compile_features(test_player_integration PRIVATE cxx_std_23)
  catch_discover_tests(test_player_integration)
endif()