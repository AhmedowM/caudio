# caudio toolchain dispatcher (CMAKE_TOOLCHAIN_FILE).
# Read before project(): selects one of the per-toolchain detail files.
# Deliberately contains no project logic -- auto detection lives in
# toolchains/auto.cmake, which delegates to the same detail files.
set(CAUDIO_TOOLCHAIN "auto" CACHE STRING
  "Toolchain to configure: auto|gcc|clang|msvc|apple")
set_property(CACHE CAUDIO_TOOLCHAIN PROPERTY STRINGS auto gcc clang msvc apple)

set(_caudio_toolchains auto gcc clang msvc apple)
if(NOT CAUDIO_TOOLCHAIN IN_LIST _caudio_toolchains)
  message(FATAL_ERROR
    "Unknown CAUDIO_TOOLCHAIN='${CAUDIO_TOOLCHAIN}'. "
    "Valid values: auto, gcc, clang, msvc, apple.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/toolchains/${CAUDIO_TOOLCHAIN}.cmake")
