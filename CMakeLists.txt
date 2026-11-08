cmake_minimum_required(VERSION 3.28)

include(cmake/version.cmake)

project(caudio VERSION ${PROJECT_VERSION} LANGUAGES C)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No C++ modules here; this avoids requiring clang-scan-deps when the optional
# C++ differential test enables CXX under CMP0155 NEW.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Co-locate DLLs and executables in the build tree so tests can find shared
# libraries at runtime without manipulating PATH.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(cmake/options.cmake)
include(cmake/ccache.cmake)
include(cmake/sanitizers.cmake)

if(CAUDIO_USE_SYSTEM_CSTDX)
  find_package(cstdx CONFIG REQUIRED)
else()
  add_subdirectory(libs/cstdx)
endif()

include(cmake/yyjson.cmake)
include(cmake/runtime.cmake)

if(CAUDIO_BUILD_TESTS)
  include(cmake/tests.cmake)
endif()

include(cmake/caudio.cmake)
include(cmake/install.cmake)
