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
# Single compilation per C++23 module: static libs own the module BMIs,
# shared variants link the static objects whole-archive (GCC -fmodules-ts
# BMI CRC covers defines/flags, so recompiling the same sources with
# different *_EXPORTS/PIC flags breaks dependents with CRC mismatch).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Options — CAUDIO_WITH_FFMPEG ON required primary (C had OFF CMakeLists.txt:62)
option(CAUDIO_WITH_FFMPEG "use FFmpeg as primary decoder" ON)
option(CAUDIO_WITH_FETCH_FFMPEG "auto-fetch FFmpeg if not found (system → vcpkg/Conan → prebuilt → source)" ON)
option(CAUDIO_ENABLE_TESTS "build tests" OFF)
option(CAUDIO_ENABLE_EXAMPLES "build examples" OFF)
option(CAUDIO_ENABLE_SANITIZERS "Enable ASan+UBSan (like C: -fsanitize=address,undefined)" OFF)

# Module path for FindFFmpeg
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Threads REQUIRED)

if(CAUDIO_WITH_FFMPEG)
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

add_library(caudio_sqlite_static STATIC vendor/sqlite3.c)
set_target_properties(caudio_sqlite_static PROPERTIES OUTPUT_NAME caudio_sqlite)
target_include_directories(caudio_sqlite_static PRIVATE vendor)
target_include_directories(caudio_sqlite_static PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_definitions(caudio_sqlite_static PUBLIC SQLITE_ENABLE_FTS5=1)

# RtAudio vendored but not compiled — miniaudio is the chosen output (plan)

# Vendor: BLAKE3 C core (portable + dispatch, no AVX/SSE - force portable)
add_library(blake3 OBJECT vendor/blake3.c vendor/blake3_dispatch.c vendor/blake3_portable.c)
target_include_directories(blake3 PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor>)
target_compile_definitions(blake3 PRIVATE BLAKE3_NO_AVX512 BLAKE3_NO_AVX2 BLAKE3_NO_SSE41 BLAKE3_NO_SSE2 BLAKE3_USE_NEON=0)
# suppress GCC warnings in vendored C
if(NOT MSVC)
  target_compile_options(blake3 PRIVATE -w)
endif()

# Warnings
set(CAUDIO_WARNING_FLAGS
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)

function(ca_set_warnings tgt)
  target_compile_options(${tgt} PRIVATE ${CAUDIO_WARNING_FLAGS})
endfunction()

# Additional warning suppressions for module-related issues on GNU
function(ca_set_module_warnings tgt)
  target_compile_options(${tgt} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-template-names-tu-local -Wno-expose-global-module-tu-local>)
endfunction()

# caudio::utils — C++23 named module partitioned, no hpp split
set(CAUDIO_UTILS_SOURCES
  src/utils/utils.cppm
  src/utils/result.cppm
  src/utils/error.cppm
  src/utils/log.cppm
  src/utils/ring.cppm
  src/utils/mpsc_queue.cppm
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

# Shared variant: no module recompilation (see note above). Links the static
# objects whole-archive so the DLL carries the symbols; module BMIs come
# from the static target exactly once.
add_library(caudio_utils_shared SHARED)
set_target_properties(caudio_utils_shared PROPERTIES OUTPUT_NAME caudio_utils)
add_library(caudio::utils_shared ALIAS caudio_utils_shared)
target_sources(caudio_utils_shared PRIVATE src/_stub/shared_stub.cpp)
target_link_libraries(caudio_utils_shared PUBLIC caudio::utils Threads::Threads)
target_link_libraries(caudio_utils_shared PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::utils>)
target_link_options(caudio_utils_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)

# caudio::player — partitioned modules + decoders (Task 3/4)
set(CAUDIO_PLAYER_SOURCES
  src/player/player.cppm
  src/player/player_core.cppm
  src/player/reader.cppm
  src/player/output.cppm
  src/player/decoder.cppm
  src/player/decoders/decoder_common.cppm
  src/player/decoders/decoder_interface.cppm
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
ca_set_warnings(caudio_player)
target_sources(caudio_player PRIVATE src/player/miniaudio_impl.cpp)
target_compile_options(caudio_player PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)

add_library(caudio_player_shared SHARED)
set_target_properties(caudio_player_shared PROPERTIES OUTPUT_NAME caudio_player)
add_library(caudio::player_shared ALIAS caudio_player_shared)
target_sources(caudio_player_shared PRIVATE src/_stub/shared_stub.cpp)
target_link_libraries(caudio_player_shared PUBLIC caudio::player caudio::utils_shared)
target_link_libraries(caudio_player_shared PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::player>)
target_link_options(caudio_player_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)
if(CAUDIO_WITH_FFMPEG AND FFmpeg_FOUND)
  target_link_libraries(caudio_player_shared PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
endif()

# nlohmann json for ordered_json
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# CLI11 for App dispatch
FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(CLI11)

# caudio::db — single caudio_sqlite STATIC, shared_mutex+Transaction later
# P3-6: centralize DB module list to avoid quartet duplication (static/shared/combined)
set(CAUDIO_DB_SOURCES
  src/db/database.cppm
  src/db/db_types.cppm
  src/db/schema.cppm
  src/db/db_core.cppm
  src/db/queue.cppm
  src/db/scan.cppm
  src/db/search.cppm
  src/db/json.cppm
  src/db/write_thread.cppm
  src/db/statement.cppm
  src/db/transaction.cppm
  src/db/detail.cppm
  src/db/fingerprint.cppm
  src/db/fts.cppm
  src/db/stmt_helpers.cppm
)
add_library(caudio_db STATIC)
add_library(caudio::db ALIAS caudio_db)
target_sources(caudio_db PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES ${CAUDIO_DB_SOURCES})
target_include_directories(caudio_db PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(caudio_db PRIVATE vendor)
target_link_libraries(caudio_db PUBLIC caudio::utils Threads::Threads)
target_include_directories(caudio_db PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
target_sources(caudio_db PRIVATE $<TARGET_OBJECTS:caudio_sqlite> $<TARGET_OBJECTS:blake3>)
target_compile_definitions(caudio_db PUBLIC SQLITE_ENABLE_FTS5=1)
ca_set_warnings(caudio_db)
ca_set_module_warnings(caudio_db)

add_library(caudio_db_shared SHARED)
set_target_properties(caudio_db_shared PROPERTIES OUTPUT_NAME caudio_db)
add_library(caudio::db_shared ALIAS caudio_db_shared)
target_sources(caudio_db_shared PRIVATE src/_stub/shared_stub.cpp)
target_link_libraries(caudio_db_shared PUBLIC caudio::db caudio::utils_shared Threads::Threads)
target_link_libraries(caudio_db_shared PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::db>)
target_link_options(caudio_db_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)

# caudio::engine
set(CAUDIO_ENGINE_SOURCES
  src/engine/engine.cppm
  src/engine/engine_types.cppm
  src/engine/history.cppm
  src/engine/shuffle.cppm
)
add_library(caudio_engine STATIC)
add_library(caudio::engine ALIAS caudio_engine)
target_sources(caudio_engine PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES ${CAUDIO_ENGINE_SOURCES})
target_include_directories(caudio_engine PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(caudio_engine PUBLIC caudio::db caudio::player caudio::utils Threads::Threads)
target_include_directories(caudio_engine PRIVATE vendor ${nlohmann_json_SOURCE_DIR}/include)
ca_set_warnings(caudio_engine)
ca_set_module_warnings(caudio_engine)

add_library(caudio_engine_shared SHARED)
set_target_properties(caudio_engine_shared PROPERTIES OUTPUT_NAME caudio_engine)
add_library(caudio::engine_shared ALIAS caudio_engine_shared)
target_sources(caudio_engine_shared PRIVATE src/_stub/shared_stub.cpp)
target_link_libraries(caudio_engine_shared PUBLIC caudio::engine caudio::db_shared caudio::player_shared caudio::utils_shared Threads::Threads)
target_link_libraries(caudio_engine_shared PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::engine>)
target_link_options(caudio_engine_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)

# Optional combined shared library: caudio_all (utils+player+db+engine)
# Links the static objects whole-archive; module BMIs come from the static
# targets exactly once (see note above).
add_library(caudio_all SHARED)
target_sources(caudio_all PRIVATE src/_stub/shared_stub.cpp)
target_link_libraries(caudio_all PUBLIC
  caudio::utils caudio::player caudio::db caudio::engine Threads::Threads)
target_link_libraries(caudio_all PRIVATE
  $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::utils>
  $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::player>
  $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::db>
  $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::engine>)
target_link_options(caudio_all PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)
if(CAUDIO_WITH_FFMPEG AND FFmpeg_FOUND)
  target_link_libraries(caudio_all PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
endif()

# caudio::json — nlohmann ordered_json wrapper module
set(CAUDIO_JSON_SOURCES
  src/json/json.cppm
)
add_library(caudio_json STATIC)
add_library(caudio::json ALIAS caudio_json)
target_sources(caudio_json PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES ${CAUDIO_JSON_SOURCES})
target_include_directories(caudio_json PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(caudio_json PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
ca_set_warnings(caudio_json)
ca_set_module_warnings(caudio_json)

add_library(caudio_json_shared SHARED)
set_target_properties(caudio_json_shared PROPERTIES OUTPUT_NAME caudio_json)
add_library(caudio::json_shared ALIAS caudio_json_shared)
target_sources(caudio_json_shared PRIVATE src/_stub/shared_stub.cpp)
target_link_libraries(caudio_json_shared PUBLIC caudio::json)
target_link_libraries(caudio_json_shared PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::json>)
target_link_options(caudio_json_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)

# caudio::cli shared protocol (command/result/protocol)
set(CAUDIO_CLI_SHARED_SOURCES
  cli/src/cli.cppm
  cli/src/shared/command.cppm
  cli/src/shared/result.cppm
  cli/src/shared/protocol.cppm
  cli/src/config.cppm
)
add_library(caudio_cli_shared STATIC)
add_library(caudio::cli_shared ALIAS caudio_cli_shared)
target_sources(caudio_cli_shared PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES ${CAUDIO_CLI_SHARED_SOURCES})
target_include_directories(caudio_cli_shared PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(caudio_cli_shared PRIVATE vendor ${nlohmann_json_SOURCE_DIR}/include)
target_link_libraries(caudio_cli_shared PUBLIC caudio::engine caudio::db caudio::utils caudio::json Threads::Threads)
ca_set_warnings(caudio_cli_shared)
ca_set_module_warnings(caudio_cli_shared)
target_compile_options(caudio_cli_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)
# nlohmann json header-only with C++20 modules emits duplicate COMDATs on MinGW; allow deduplication
target_link_options(caudio_cli_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)

# caudio::service — IPC channel, server, shm, implementation (feat/cli-service)
set(CAUDIO_SERVICE_SOURCES
  cli/src/service/service.cppm
  cli/src/service/service_detail.cppm
  cli/src/service/service_impl.cppm
  cli/src/service/shm_status.cppm
  cli/src/service/ipc_channel.cppm
  cli/src/service/ipc_channel_unix.cpp
  cli/src/service/ipc_channel_win.cpp
  cli/src/service/ipc_server.cppm
)
add_library(caudio_service STATIC)
add_library(caudio::service ALIAS caudio_service)
target_sources(caudio_service PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES ${CAUDIO_SERVICE_SOURCES})
target_include_directories(caudio_service PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(caudio_service PRIVATE vendor ${nlohmann_json_SOURCE_DIR}/include)
target_link_libraries(caudio_service PUBLIC caudio::cli_shared caudio::engine caudio::db caudio::utils Threads::Threads)
if(NOT WIN32)
  find_library(LIBRT rt)
  if(LIBRT)
    target_link_libraries(caudio_service PUBLIC ${LIBRT})
  endif()
endif()
ca_set_warnings(caudio_service)
ca_set_module_warnings(caudio_service)
target_compile_options(caudio_service PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)

add_library(caudio_service_shared SHARED)
set_target_properties(caudio_service_shared PROPERTIES OUTPUT_NAME caudio_service)
add_library(caudio::service_shared ALIAS caudio_service_shared)
target_sources(caudio_service_shared PRIVATE src/_stub/shared_stub.cpp)
target_link_libraries(caudio_service_shared PUBLIC caudio::service caudio::cli_shared caudio::engine_shared caudio::db_shared caudio::utils_shared Threads::Threads)
target_link_libraries(caudio_service_shared PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::service>)
target_link_options(caudio_service_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)
if(NOT WIN32)
  find_library(LIBRT rt)
  if(LIBRT)
    target_link_libraries(caudio_service_shared PUBLIC ${LIBRT})
  endif()
endif()

# caudio::client — IpcClient wrapper + output formatter (feat/cli-service)
set(CAUDIO_CLIENT_SOURCES
  cli/src/client/client.cppm
  cli/src/client/ipc_client.cppm
  cli/src/client/client_impl.cppm
  cli/src/client/output_formatter.cppm
)
add_library(caudio_client STATIC)
add_library(caudio::client ALIAS caudio_client)
target_sources(caudio_client PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES ${CAUDIO_CLIENT_SOURCES})
target_include_directories(caudio_client PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(caudio_client PRIVATE vendor ${nlohmann_json_SOURCE_DIR}/include)
target_link_libraries(caudio_client PUBLIC caudio::cli_shared caudio::utils caudio::service Threads::Threads)
ca_set_warnings(caudio_client)
ca_set_module_warnings(caudio_client)
target_compile_options(caudio_client PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-global-module>)

add_library(caudio_client_shared SHARED)
set_target_properties(caudio_client_shared PROPERTIES OUTPUT_NAME caudio_client)
add_library(caudio::client_shared ALIAS caudio_client_shared)
target_sources(caudio_client_shared PRIVATE src/_stub/shared_stub.cpp)
target_link_libraries(caudio_client_shared PUBLIC caudio::client caudio::cli_shared caudio::utils_shared caudio::service_shared Threads::Threads)
target_link_libraries(caudio_client_shared PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,caudio::client>)
target_link_options(caudio_client_shared PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)

# caudio executable — App dispatch (CLI11) + main entry
add_executable(caudio
  cli/src/main.cpp
)
target_sources(caudio PUBLIC FILE_SET CXX_MODULES TYPE CXX_MODULES FILES cli/src/app/app.cppm cli/src/app/core.cppm)
target_include_directories(caudio PRIVATE cli/src/app vendor ${nlohmann_json_SOURCE_DIR}/include ${CLI11_SOURCE_DIR}/include)
target_include_directories(caudio PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(caudio PRIVATE caudio_service caudio_client caudio_cli_shared CLI11::CLI11 Threads::Threads)
if(CAUDIO_WITH_FFMPEG AND FFmpeg_FOUND)
  target_link_libraries(caudio PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swresample)
endif()
ca_set_warnings(caudio)
ca_set_module_warnings(caudio)
# Needed on MinGW for template instantiation deduplication across executable + static libs
target_link_options(caudio PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)
# GCC 14+ <print> requires libstdc++exp on MinGW/GCC
if(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  find_library(STDCXX_EXP_LIB stdc++exp)
  if(STDCXX_EXP_LIB)
    target_link_libraries(caudio PRIVATE stdc++exp)
    target_link_libraries(caudio_client PRIVATE stdc++exp)
    target_link_libraries(caudio_cli_shared PRIVATE stdc++exp)
  endif()
endif()

# Install / Export / Packaging like C CMakeLists.txt:334-382
include(GNUInstallDirs)
install(
  TARGETS
    caudio_utils caudio_utils_shared
    caudio_player caudio_player_shared
    caudio_db caudio_db_shared
    caudio_engine caudio_engine_shared
    caudio_json caudio_json_shared
    caudio_cli_shared
    caudio_client caudio_client_shared
    caudio_service caudio_service_shared
    caudio_all
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

  # IPC/Cli integration tests
  add_executable(test_ipc tests/test_ipc.cpp)
  target_link_libraries(test_ipc PRIVATE caudio::service caudio::client caudio::cli_shared caudio::engine caudio::db caudio::player caudio::utils Catch2::Catch2WithMain caudio_sqlite_static)
  target_include_directories(test_ipc PRIVATE vendor ${nlohmann_json_SOURCE_DIR}/include)
  target_compile_features(test_ipc PRIVATE cxx_std_23)
  target_link_options(test_ipc PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--allow-multiple-definition>)
  catch_discover_tests(test_ipc)
endif()