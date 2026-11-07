# Resolve the cstdx version and generate <cstdx/version.h>.
#
# Version sources, in priority order:
#   1. -DCSTDX_PROJECT_VERSION=X.Y.Z[-suffix]
#   2. The nearest vX.Y.Z git tag
#   3. libs/cstdx/VERSION
#   4. Fallback 0.1.0 with a warning
#
# This file must be included before project(cstdx ...) so PROJECT_VERSION is
# available to the project() command.

option(CSTDX_REQUIRE_GIT_VERSION
    "require a git tag to be present for cstdx versioning" OFF)

set(CSTDX_VERSION_SUFFIX "")

# Split a version string into major, minor, patch and an optional suffix.
macro(_cstdx_split_version INPUT OUT_OK)
  if("${INPUT}" MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.-]+)?$")
    set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}")
    set(PROJECT_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    set(CSTDX_VERSION_SUFFIX "${CMAKE_MATCH_4}")
    set(${OUT_OK} TRUE)
  else()
    set(${OUT_OK} FALSE)
  endif()
endmacro()

if(DEFINED CSTDX_PROJECT_VERSION AND NOT CSTDX_PROJECT_VERSION STREQUAL "")
  _cstdx_split_version("${CSTDX_PROJECT_VERSION}" _cstdx_ver_ok)
  if(NOT _cstdx_ver_ok)
    message(FATAL_ERROR
        "CSTDX_PROJECT_VERSION='${CSTDX_PROJECT_VERSION}' is not "
        "X.Y.Z[-suffix] (optional leading v)")
  endif()
  message(STATUS "Using explicit cstdx version: ${PROJECT_VERSION}${CSTDX_VERSION_SUFFIX} (-DCSTDX_PROJECT_VERSION)")
else()
  execute_process(
    COMMAND git describe --tags --abbrev=0 --match "v[0-9]*" --dirty
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  _cstdx_split_version("${GIT_TAG}" _cstdx_ver_ok)
  if(NOT _cstdx_ver_ok)
    set(GIT_TAG "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
      file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _cstdx_version_file)
      string(STRIP "${_cstdx_version_file}" _cstdx_version_file)
      _cstdx_split_version("${_cstdx_version_file}" _cstdx_ver_ok)
    endif()
    if(_cstdx_ver_ok)
      message(STATUS "No cstdx git tag -- using VERSION file: ${PROJECT_VERSION}${CSTDX_VERSION_SUFFIX}")
    else()
      if(CSTDX_REQUIRE_GIT_VERSION)
        message(FATAL_ERROR
            "CSTDX_REQUIRE_GIT_VERSION=ON but no git tag found -- clone with "
            "tags (git fetch --tags) or configure with "
            "-DCSTDX_REQUIRE_GIT_VERSION=OFF")
      endif()
      message(WARNING
          "No cstdx git tag and no usable VERSION file -- using fallback "
          "version 0.1.0. Tag-less builds are NOT release builds.")
      set(PROJECT_VERSION_MAJOR "0")
      set(PROJECT_VERSION_MINOR "1")
      set(PROJECT_VERSION_PATCH "0")
      set(PROJECT_VERSION "0.1.0")
      set(CSTDX_VERSION_SUFFIX "")
    endif()
  endif()
endif()

execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  OUTPUT_VARIABLE CSTDX_GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT CSTDX_GIT_HASH)
  set(CSTDX_GIT_HASH "unknown")
endif()

set(CSTDX_VERSION_FULL "${PROJECT_VERSION}${CSTDX_VERSION_SUFFIX}")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/include/cstdx/version.h"
  @ONLY
)

unset(_cstdx_version_file)
unset(_cstdx_ver_ok)
