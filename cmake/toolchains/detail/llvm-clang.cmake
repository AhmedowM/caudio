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

# Force brew's own libc++ headers and library. The SDK copy lags our C++23
# baseline (`std::expected`, `std::print`); nothing else reliably redirects
# the search there. -nostdinc++ drops every default C++ include (compiler
# intrinsics in the resource dir are unaffected), -cxx-isystem adds exactly
# ours ahead of any -isystem (e.g. FFmpeg's /opt/homebrew/include) so brew
# headers always win regardless of flag order. -stdlib=libc++ is needed at
# both compile (header selection) and link time. -L + rpath pair the matching
# dylib so binaries never mix new headers with the old system libc++ at
# runtime. Layout varies (lib/ vs lib/c++/, versioned dylibs), so derive the
# lib dir from the dylib itself instead of assuming it.
# NOTE: `std::move_only_function` and `std::generator` have no libc++
# implementation at all (missing even in LLVM 23); they are polyfilled
# in-tree (utils/function.hpp, utils/generator.hpp), not a header-search
# issue.
if(NOT EXISTS "${_caudio_llvm_root}/include/c++/v1/__config")
  message(FATAL_ERROR
    "brew LLVM libc++ headers not found under ${_caudio_llvm_root}. "
    "Reinstall with: brew reinstall llvm")
endif()
file(GLOB _caudio_libcxx
  "${_caudio_llvm_root}/lib/libc++*.dylib"
  "${_caudio_llvm_root}/lib/c++/libc++*.dylib")
if(NOT _caudio_libcxx)
  message(FATAL_ERROR
    "brew LLVM libc++ library not found under ${_caudio_llvm_root}. "
    "Reinstall with: brew reinstall llvm")
endif()
list(GET _caudio_libcxx 0 _caudio_libcxx_first)
get_filename_component(_caudio_libcxx_dir "${_caudio_libcxx_first}" DIRECTORY)
add_compile_options(
  $<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>
  $<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>
  $<$<COMPILE_LANGUAGE:CXX>:-cxx-isystem>
  $<$<COMPILE_LANGUAGE:CXX>:${_caudio_llvm_root}/include/c++/v1>)
add_link_options(
  -stdlib=libc++
  "-L${_caudio_libcxx_dir}"
  "-Wl,-rpath,${_caudio_libcxx_dir}")
unset(_caudio_llvm_root)
unset(_caudio_brew_rc)
unset(_cand)
unset(_caudio_libcxx)
unset(_caudio_libcxx_first)
unset(_caudio_libcxx_dir)
