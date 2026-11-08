# cstdx project options and sanitizer configuration.

option(CSTDX_BUILD_TESTS "Build cstdx tests" ${PROJECT_IS_TOP_LEVEL})
option(CSTDX_BUILD_CPP_TESTS "Build C++ differential/property tests" OFF)
option(CSTDX_BUILD_FUZZ "Build libFuzzer targets (Clang only)" OFF)
option(CSTDX_BUILD_SHARED "Build cstdx as a shared library" ON)
option(CSTDX_BUILD_BENCHMARKS "Build cstdx benchmarks" OFF)
option(CSTDX_ENABLE_COVERAGE
    "Instrument with Clang source-based coverage for llvm-cov (Clang only)" OFF)

if(CSTDX_ENABLE_COVERAGE)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "CSTDX_ENABLE_COVERAGE=ON requires Clang for -fprofile-instr-generate "
        "and -fcoverage-mapping; CMAKE_C_COMPILER_ID is "
        "'${CMAKE_C_COMPILER_ID}'. Configure with CC=clang (or clang-19).")
  endif()
  add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
  add_link_options(-fprofile-instr-generate)
  message(STATUS "cstdx coverage instrumentation: llvm-cov")
endif()

set(CSTDX_SANITIZERS "" CACHE STRING
    "Sanitizers to enable, comma-separated: address,undefined,thread,memory,leak \
(asan/ubsan/tsan/msan aliases are accepted)")
option(CSTDX_ENABLE_SANITIZERS "Deprecated alias for CSTDX_SANITIZERS=address,undefined" OFF)

if(CSTDX_ENABLE_SANITIZERS AND NOT CSTDX_SANITIZERS)
  set(CSTDX_SANITIZERS "address,undefined")
  message(DEPRECATION
      "CSTDX_ENABLE_SANITIZERS is deprecated; use -DCSTDX_SANITIZERS=address,undefined")
endif()

if(CSTDX_SANITIZERS AND NOT PROJECT_IS_TOP_LEVEL)
  message(WARNING
      "CSTDX_SANITIZERS only instruments cstdx when it is built as a subproject. "
      "Set the parent project's sanitizer option as well so every target linked "
      "into the same binary is instrumented consistently.")
endif()

# Applied once, at directory scope, before any target is created. This covers
# the library, the tests and anything FetchContent adds under this project.
string(REPLACE ";" "," _cstdx_sanitizers "${CSTDX_SANITIZERS}")
string(TOLOWER "${_cstdx_sanitizers}" _cstdx_sanitizers)
string(REPLACE " " "" _cstdx_sanitizers "${_cstdx_sanitizers}")

if(_cstdx_sanitizers)
  string(REPLACE "," ";" _cstdx_sanitizer_list "${_cstdx_sanitizers}")
  set(_cstdx_sanitizer_ids "")
  foreach(_sanitizer IN LISTS _cstdx_sanitizer_list)
    if(_sanitizer STREQUAL "")
      continue()
    endif()

    if(_sanitizer STREQUAL "asan")
      set(_sanitizer address)
    elseif(_sanitizer STREQUAL "ubsan")
      set(_sanitizer undefined)
    elseif(_sanitizer STREQUAL "tsan")
      set(_sanitizer thread)
    elseif(_sanitizer STREQUAL "msan")
      set(_sanitizer memory)
    endif()

    if(NOT _sanitizer MATCHES "^(address|undefined|thread|memory|leak)$")
      message(FATAL_ERROR
          "CSTDX_SANITIZERS: unsupported sanitizer '${_sanitizer}' "
          "(expected address, undefined, thread, memory or leak)")
    endif()

    list(APPEND _cstdx_sanitizer_ids "${_sanitizer}")
  endforeach()
  list(REMOVE_DUPLICATES _cstdx_sanitizer_ids)
  list(JOIN _cstdx_sanitizer_ids "," _cstdx_sanitizer_arg)

  if(MSVC)
    if(NOT _cstdx_sanitizer_arg STREQUAL "address")
      message(FATAL_ERROR "CSTDX_SANITIZERS: MSVC only supports 'address'")
    endif()
    add_compile_options(/fsanitize=address)
    add_link_options(/fsanitize=address)
  else()
    set(_cstdx_sanitize_flags
        "-fsanitize=${_cstdx_sanitizer_arg}"
        -fno-omit-frame-pointer
        -g
    )
    if(_cstdx_sanitizer_arg MATCHES "undefined")
      list(APPEND _cstdx_sanitize_flags -fno-sanitize-recover=all)
    endif()
    add_compile_options(${_cstdx_sanitize_flags})
    add_link_options("-fsanitize=${_cstdx_sanitizer_arg}")
  endif()

  message(STATUS "cstdx sanitizers: ${_cstdx_sanitizer_arg}")
endif()
unset(_cstdx_sanitizers)
unset(_cstdx_sanitizer_list)
unset(_cstdx_sanitizer_ids)
unset(_cstdx_sanitizer_arg)
unset(_cstdx_sanitize_flags)
unset(_sanitizer)
