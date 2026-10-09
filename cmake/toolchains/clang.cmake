# Clang toolchain (local dev / perf comparison only; CI/Release is GCC).
# NOTE: native target everywhere. On Windows that is the MSVC ABI (verified:
# full build + 143/143 with zero extra flags; sized deallocation is default
# there, and lld-link consumes the same .dll.a FFmpeg import libs as the
# MinGW builds). No --target override, no extra flags.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  include("${CMAKE_CURRENT_LIST_DIR}/detail/llvm-clang.cmake")
else()
  set(CMAKE_C_COMPILER clang)
  set(CMAKE_CXX_COMPILER clang++)
endif()

# Strip release installs (smaller shipped binaries); dev installs keep
# symbols. Guarded on a strip tool existing (MSVC-ABI builds may have
# none -- those exes are lean already).
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  if(NOT CMAKE_STRIP)
    find_program(CMAKE_STRIP NAMES strip llvm-strip)
  endif()
  if(CMAKE_STRIP)
    set(CMAKE_INSTALL_DO_STRIP ON CACHE BOOL "Strip binaries on install (release)")
  endif()
endif()
