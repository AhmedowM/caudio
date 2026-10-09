# Canonical toolchain: GCC is the only CI/Release compiler.
# Pins the compiler explicitly so builds don't depend on PATH order.
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

# Strip release installs (smaller shipped binaries); dev installs keep
# symbols. Guarded on a strip tool existing so exotic setups configure.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  if(NOT CMAKE_STRIP)
    find_program(CMAKE_STRIP NAMES strip)
  endif()
  if(CMAKE_STRIP)
    set(CMAKE_INSTALL_DO_STRIP ON CACHE BOOL "Strip binaries on install (release)")
  endif()
endif()
