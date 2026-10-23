# AppleClang toolchain (--toolchain apple): resolves the Xcode compilers
# via xcrun so the resolver pins no machine-specific paths. No brew LLVM.
find_program(_caudio_xcrun xcrun REQUIRED)
execute_process(COMMAND "${_caudio_xcrun}" -f clang
  OUTPUT_VARIABLE _caudio_apple_cc OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _caudio_apple_cc_rc)
execute_process(COMMAND "${_caudio_xcrun}" -f clang++
  OUTPUT_VARIABLE _caudio_apple_cxx OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _caudio_apple_cxx_rc)
if(NOT _caudio_apple_cc_rc EQUAL 0 OR NOT _caudio_apple_cxx_rc EQUAL 0)
  message(FATAL_ERROR "xcrun could not resolve Xcode clang/clang++ -- install Xcode command line tools")
endif()
set(CMAKE_C_COMPILER "${_caudio_apple_cc}")
set(CMAKE_CXX_COMPILER "${_caudio_apple_cxx}")
unset(_caudio_xcrun)
unset(_caudio_apple_cc)
unset(_caudio_apple_cxx)
unset(_caudio_apple_cc_rc)
unset(_caudio_apple_cxx_rc)
