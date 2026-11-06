cmake_minimum_required(VERSION 3.28)

include(cmake/version.cmake)

project(caudio VERSION ${PROJECT_VERSION} LANGUAGES C)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CAUDIO_BUILD_TESTS "Build unit tests" ON)

if(CAUDIO_BUILD_TESTS)
  enable_testing()
  # cstdx is added as a subdirectory, so its own default (top-level only)
  # would disable its tests. Propagate the parent's choice instead.
  set(CSTDX_BUILD_TESTS ON)
endif()

option(CAUDIO_USE_SYSTEM_CSTDX "Use an installed cstdx instead of the in-tree copy" OFF)
if(CAUDIO_USE_SYSTEM_CSTDX)
  find_package(cstdx CONFIG REQUIRED)
else()
  add_subdirectory(libs/cstdx)
endif()

option(CAUDIO_ENABLE_CCACHE "use ccache/sccache compiler launcher if found (opt-in)" ON)

if(CAUDIO_ENABLE_CCACHE)
  foreach(_launcher ccache sccache)
    find_program(_CAUDIO_LAUNCHER ${_launcher})
    if(_CAUDIO_LAUNCHER)
      set(CMAKE_C_COMPILER_LAUNCHER ${_CAUDIO_LAUNCHER})
      message(STATUS "Compiler launcher: ${_CAUDIO_LAUNCHER}")
      break()
    endif()
  endforeach()
  if(NOT CMAKE_C_COMPILER_LAUNCHER)
    message(WARNING "CAUDIO_ENABLE_CCACHE=ON but neither ccache nor sccache found -- compiling without launcher")
  endif()
endif()
unset(_launcher)