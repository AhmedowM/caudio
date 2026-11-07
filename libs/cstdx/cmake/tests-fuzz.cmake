# libFuzzer targets.
#
# Clang-only: libFuzzer is a compiler-rt component. Enable with
#   -DCSTDX_BUILD_FUZZ=ON -DCMAKE_C_COMPILER=clang
#
# Targets: cx_base64_decode, cx_string_builder/cx_string, cx_vector ops.

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR
      "CSTDX_BUILD_FUZZ=ON requires Clang (libFuzzer); CMAKE_C_COMPILER_ID is "
      "'${CMAKE_C_COMPILER_ID}'")
endif()

set(CSTDX_FUZZ_COMPILE_FLAGS
    -fsanitize=fuzzer-no-link,address,undefined
    -fno-omit-frame-pointer
    -g
)
set(CSTDX_FUZZ_LINK_FLAGS
    -fsanitize=fuzzer,address,undefined
)

# A separately compiled, sanitizer-instrumented copy of the library. The main
# cstdx target must stay uninstrumented so the regular unit tests can link
# against it in the same build tree.
set(_fuzz_sources ${CSTDX_SOURCES})

add_library(cstdx_fuzz_support STATIC ${_fuzz_sources})
target_include_directories(cstdx_fuzz_support
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_BINARY_DIR}/include
)
target_compile_features(cstdx_fuzz_support PUBLIC c_std_23)
target_compile_options(cstdx_fuzz_support PRIVATE ${CSTDX_FUZZ_COMPILE_FLAGS})

set(_cstdx_fuzz_dir ${PROJECT_SOURCE_DIR}/tests/fuzz)

set(CSTDX_FUZZ_TARGETS
    base64
    string
    vector
)

foreach(_target IN LISTS CSTDX_FUZZ_TARGETS)
  add_executable(fuzz_${_target} ${_cstdx_fuzz_dir}/fuzz_${_target}.c)
  target_link_libraries(fuzz_${_target} PRIVATE cstdx_fuzz_support)
  target_compile_options(fuzz_${_target} PRIVATE ${CSTDX_FUZZ_COMPILE_FLAGS})
  target_link_options(fuzz_${_target} PRIVATE ${CSTDX_FUZZ_LINK_FLAGS})

  add_test(NAME cstdx.fuzz.${_target}.smoke COMMAND fuzz_${_target} -runs=500)
  set_tests_properties(cstdx.fuzz.${_target}.smoke PROPERTIES
      LABELS fuzz
      TIMEOUT 120
  )
endforeach()

unset(_cstdx_fuzz_dir)
unset(_fuzz_sources)
unset(_source)
unset(CSTDX_FUZZ_COMPILE_FLAGS)
unset(CSTDX_FUZZ_LINK_FLAGS)
unset(CSTDX_FUZZ_TARGETS)
unset(_target)
