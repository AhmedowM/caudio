# Default toolchain selector: picks the canonical toolchain for this host
# so bare presets (dev, ci, release, ...) mean "the right compiler here".
# Delegates to the sibling files -- single source of truth per toolchain,
# no compiler assignments duplicated here. Explicit -gcc/-clang/-msvc
# presets override this via merge precedence (their mixin is listed first
# in `inherits`).
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  include("${CMAKE_CURRENT_LIST_DIR}/clang.cmake")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  if(CMAKE_GENERATOR MATCHES "Visual Studio" OR DEFINED ENV{VCINSTALLDIR})
    include("${CMAKE_CURRENT_LIST_DIR}/msvc.cmake")
  else()
    if(NOT _caudio_auto_msg_shown)
      message(STATUS "auto toolchain: no VS environment; MinGW GCC from PATH "
        "(use a *-msvc preset or a vcvars shell for MSVC)")
      set(_caudio_auto_msg_shown TRUE)
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/gcc.cmake")
  endif()
else()
  include("${CMAKE_CURRENT_LIST_DIR}/gcc.cmake")
endif()
