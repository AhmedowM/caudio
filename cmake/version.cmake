# Find the version from git tags, or from a VERSION file, or from an explicit override
# The version is expected to be in the form X.Y.Z[-suffix] (optional leading v)
# The suffix is optional and can be used to indicate pre-release versions, e.g. "-beta1", "-rc2", etc.
# Explicit override can be provided via -DCAUDIO_PROJECT_VERSION=X.Y.Z[-suffix], and will take precedence over git tags or VERSION file.
# If no version can be found, a fallback version of 0.1.0 will be used, and a warning will be issued.

option(CAUDIO_REQUIRE_GIT_VERSION "require a git tag to be present for versioning" OFF)

set(CAUDIO_VERSION_SUFFIX "")

# Helper macro to split version string into major, minor, patch, and optional suffix
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

# Override version if CAUDIO_PROJECT_VERSION is defined
if(DEFINED CAUDIO_PROJECT_VERSION AND NOT CAUDIO_PROJECT_VERSION STREQUAL "")
  _caudio_split_version("${CAUDIO_PROJECT_VERSION}" _caudio_ver_ok)
  if(NOT _caudio_ver_ok)
    message(FATAL_ERROR "PROJECT_VERSION='${CAUDIO_PROJECT_VERSION}' is not X.Y.Z[-suffix] (optional leading v)")
  endif()
  set(GIT_TAG "v${PROJECT_VERSION}${CAUDIO_VERSION_SUFFIX}")
  message(STATUS "Using explicit version override: ${PROJECT_VERSION}${CAUDIO_VERSION_SUFFIX} (-DC AUDIO_PROJECT_VERSION)")
else() # No explicit override, try to get version from git tag or VERSION file
  execute_process(
    COMMAND git describe --tags --abbrev=0 --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  _caudio_split_version("${GIT_TAG}" _caudio_ver_ok)
  if(NOT _caudio_ver_ok) # No git tag found, try to read from VERSION file
    set(GIT_TAG "")
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION_FILE_CONTENT)
        string(STRIP "${VERSION_FILE_CONTENT}" VERSION_FILE_CONTENT)
        _caudio_split_version("${VERSION_FILE_CONTENT}" _caudio_ver_ok)
    endif()
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

# Generate version.h from version.h.in
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/caudio/version.h
  @ONLY
)