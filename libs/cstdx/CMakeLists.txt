cmake_minimum_required(VERSION 3.28)

project(cstdx VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CSTDX_BUILD_TESTS "Build cstdx tests" ${PROJECT_IS_TOP_LEVEL})
option(CSTDX_BUILD_CPP_TESTS "Build C++ differential/property tests" OFF)
option(CSTDX_BUILD_FUZZ "Build libFuzzer targets (Clang only)" OFF)
option(CSTDX_BUILD_SHARED "Build cstdx as a shared library" ON)
option(CSTDX_ENABLE_SANITIZERS "Enable AddressSanitizer + UBSan" OFF)
option(CSTDX_BUILD_BENCHMARKS "Build cstdx benchmarks" OFF)

# ---------------------------------------------------------------------------
# Library
#
# Layout only at this point: most translation units are placeholders. The
# source list is complete so that adding an implementation never requires a
# CMake change.
# ---------------------------------------------------------------------------

set(CSTDX_SOURCES
    src/status.c
    src/checked.c

    src/result.c
    src/expected.c

    src/allocator.c
    src/arena.c
    src/pool.c
    src/allocator_counted.c
    src/allocator_canary.c

    src/slice.c
    src/bytes.c
    src/string_view.c
    src/string.c
    src/string_builder.c

    src/vector.c

    src/hash.c
    src/sort.c
    src/base64.c
)

set(CSTDX_PUBLIC_HEADERS
    include/cstdx/api.h
    include/cstdx/cstdx.h

    include/cstdx/status.h
    include/cstdx/checked.h

    include/cstdx/error.h
    include/cstdx/result.h
    include/cstdx/expected.h

    include/cstdx/allocator.h
    include/cstdx/arena.h
    include/cstdx/pool.h
    include/cstdx/allocator_counted.h
    include/cstdx/allocator_canary.h

    include/cstdx/slice.h
    include/cstdx/bytes.h
    include/cstdx/string_view.h
    include/cstdx/string.h
    include/cstdx/string_builder.h

    include/cstdx/vector.h

    include/cstdx/hash.h
    include/cstdx/sort.h
    include/cstdx/base64.h
)

# Co-locate DLLs and executables in the build tree on Windows so tests can find
# shared libraries at runtime without manipulating PATH. Must be set before
# add_library() takes effect.
if(WIN32 AND CSTDX_BUILD_SHARED)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

if(CSTDX_BUILD_SHARED)
  add_library(cstdx SHARED ${CSTDX_SOURCES} ${CSTDX_PUBLIC_HEADERS})
  target_compile_definitions(cstdx
      PUBLIC  CSTDX_SHARED
      PRIVATE CSTDX_EXPORTS
  )
else()
  add_library(cstdx STATIC ${CSTDX_SOURCES} ${CSTDX_PUBLIC_HEADERS})
  target_compile_definitions(cstdx PRIVATE CSTDX_EXPORTS)
endif()

add_library(cstdx::cstdx ALIAS cstdx)

if(CSTDX_ENABLE_SANITIZERS)
  target_compile_options(cstdx PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all -Werror)
  target_link_options(cstdx PRIVATE -fsanitize=address,undefined)
endif()

target_include_directories(cstdx
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(cstdx PUBLIC c_std_23)

set_target_properties(cstdx PROPERTIES
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

if(MSVC)
  target_compile_options(cstdx PRIVATE /W4)
else()
  target_compile_options(cstdx PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(CSTDX_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------

if(CSTDX_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

# ---------------------------------------------------------------------------
# Install and export
#
# Consumers use `find_package(cstdx CONFIG REQUIRED)` and link
# `cstdx::cstdx`. The package is relocatable: the config file is generated
# from `cmake/cstdxConfig.cmake.in` and only refers to the installed tree.
# ---------------------------------------------------------------------------

include(CMakePackageConfigHelpers)

install(TARGETS cstdx
    EXPORT cstdxTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/cstdx
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT cstdxTargets
    FILE cstdxTargets.cmake
    NAMESPACE cstdx::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cstdx
)

configure_package_config_file(
    cmake/cstdxConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/cstdxConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cstdx
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cstdxConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cstdxConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cstdxConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cstdx
)
