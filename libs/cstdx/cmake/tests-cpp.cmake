# C++23 differential/oracle tests.
#
# These tests are test-only: cstdx itself stays a pure C23 library and is never
# linked with C++. GoogleTest provides the assertions and RapidCheck provides
# the property generators. Both are fetched on demand so consumers of the
# library do not pay for them.
#
# Enable with -DCSTDX_BUILD_CPP_TESTS=ON (for example in CI).

enable_language(CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

find_package(GTest QUIET)
if(NOT GTest_FOUND)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(googletest)
endif()

find_package(rapidcheck QUIET)
if(NOT rapidcheck_FOUND)
  set(RC_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
  set(RC_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(RC_ENABLE_GTEST ON CACHE BOOL "" FORCE)
  FetchContent_Declare(rapidcheck
    GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
    GIT_TAG 2c3c4365aca21ef4e612768fdc95b1ce8b39a651
  )
  FetchContent_MakeAvailable(rapidcheck)
endif()

set(_cstdx_cpp_tests_dir ${PROJECT_SOURCE_DIR}/tests/cpp)

set(CSTDX_DIFFERENTIAL_TESTS
    string
    bytes
    vector
    base64
)

foreach(_test IN LISTS CSTDX_DIFFERENTIAL_TESTS)
  add_executable(cstdx_diff_${_test}
      ${_cstdx_cpp_tests_dir}/diff_${_test}.cpp
      ${_cstdx_cpp_tests_dir}/diff_common.h
  )
  target_link_libraries(cstdx_diff_${_test}
      PRIVATE cstdx::cstdx GTest::gtest_main rapidcheck_gtest)
  target_compile_features(cstdx_diff_${_test} PRIVATE cxx_std_23)

  add_test(NAME cstdx.diff.${_test} COMMAND cstdx_diff_${_test})
  set_tests_properties(cstdx.diff.${_test} PROPERTIES LABELS differential)
endforeach()

unset(_cstdx_cpp_tests_dir)
unset(CSTDX_DIFFERENTIAL_TESTS)
unset(_test)
