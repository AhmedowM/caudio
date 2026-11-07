# Compiler launcher (ccache/sccache).
#
# Must be configured before any targets are created: CMAKE_<LANG>_COMPILER_LAUNCHER
# initialises target properties when the target is created.

if(CAUDIO_ENABLE_CCACHE)
  foreach(_launcher ccache sccache)
    find_program(_CAUDIO_LAUNCHER ${_launcher})
    if(_CAUDIO_LAUNCHER)
      set(CMAKE_C_COMPILER_LAUNCHER "${_CAUDIO_LAUNCHER}")
      set(CMAKE_CXX_COMPILER_LAUNCHER "${_CAUDIO_LAUNCHER}")
      message(STATUS "Compiler launcher: ${_CAUDIO_LAUNCHER}")
      break()
    endif()
  endforeach()
  if(NOT CMAKE_C_COMPILER_LAUNCHER)
    message(WARNING "CAUDIO_ENABLE_CCACHE=ON but neither ccache nor sccache found -- compiling without launcher")
  endif()
endif()
unset(_launcher)
unset(_CAUDIO_LAUNCHER)
