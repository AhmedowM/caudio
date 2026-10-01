# Compiler policy backstop: preset conditions guide selection, this enforces
# the hard rules for raw/custom configures that bypass presets.
option(CAUDIO_ALLOW_UNSUPPORTED_COMPILER
  "allow known-bad compiler/host combos (no audio on macOS/GCC)" OFF)

if(APPLE)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
     AND NOT CAUDIO_ALLOW_UNSUPPORTED_COMPILER)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      message(FATAL_ERROR "GCC on macOS builds miniaudio with the dummy "
        "backend (no audio). Use a *-clang preset (brew LLVM) or pass "
        "-DCAUDIO_ALLOW_UNSUPPORTED_COMPILER=ON.")
    else()
      message(FATAL_ERROR "Only LLVM Clang is supported on macOS "
        "(this is ${CMAKE_CXX_COMPILER_ID}). AppleClang lags our C++23 "
        "baseline. Use a *-clang preset or pass "
        "-DCAUDIO_ALLOW_UNSUPPORTED_COMPILER=ON.")
    endif()
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-stdlib=libc++)
    add_link_options(-stdlib=libc++)
  endif()
elseif(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(WARNING "MinGW GCC works but ships larger binaries (+winpthread "
    "DLL); *-msvc presets are preferred for releases.")
endif()
# Linux: GCC default, Clang first-class -- silence is the policy.
