cmake_minimum_required(VERSION 3.28)

include(cmake/version.cmake)

project(cstdx VERSION ${PROJECT_VERSION} LANGUAGES C)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# cstdx uses no C++ modules. With CMP0155 NEW, CMake enables module
# dependency scanning for Clang and then requires clang-scan-deps, which is a
# separate package. Disable scanning so a plain clang toolchain is enough.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# All build logic lives in cmake/. This file is the entry point so cstdx stays
# independently buildable with `cmake -S libs/cstdx`.
include(cmake/options.cmake)
include(cmake/library.cmake)

if(CSTDX_BUILD_TESTS)
  enable_testing()
  include(cmake/tests.cmake)
endif()

if(CSTDX_BUILD_BENCHMARKS)
  include(cmake/benchmarks.cmake)
endif()

include(cmake/install.cmake)
