# Resolve brew LLVM Clang on macOS (shared by toolchain-clang and
# toolchain-auto). Plain `clang` on PATH is AppleClang, which lags our C++23
# baseline; GCC builds miniaudio with a dummy backend (no audio).
if(DEFINED ENV{LLVM_ROOT})
  set(_caudio_llvm_root "$ENV{LLVM_ROOT}")
else()
  execute_process(COMMAND brew --prefix llvm
    OUTPUT_VARIABLE _caudio_llvm_root OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE _caudio_brew_rc)
  if(NOT _caudio_brew_rc EQUAL 0)
    foreach(_cand /opt/homebrew/opt/llvm /usr/local/opt/llvm)
      if(EXISTS "${_cand}/bin/clang")
        set(_caudio_llvm_root "${_cand}")
        break()
      endif()
    endforeach()
  endif()
endif()
if(NOT EXISTS "${_caudio_llvm_root}/bin/clang")
  message(FATAL_ERROR
    "LLVM Clang required on macOS (brew install llvm). "
    "AppleClang lags our C++23 baseline; GCC yields a dummy audio backend. "
    "Experts: set CMAKE_C/CXX_COMPILER manually and pass "
    "-DCAUDIO_ALLOW_UNSUPPORTED_COMPILER=ON.")
endif()
set(CMAKE_C_COMPILER "${_caudio_llvm_root}/bin/clang")
set(CMAKE_CXX_COMPILER "${_caudio_llvm_root}/bin/clang++")
unset(_caudio_llvm_root)
unset(_caudio_brew_rc)
unset(_cand)
