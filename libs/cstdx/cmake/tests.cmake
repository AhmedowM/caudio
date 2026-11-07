# cstdx tests
#
# The C suite runs everywhere. The C++23 differential oracle and the libFuzzer
# targets are opt-in because they need extra toolchains/dependencies:
#   -DCSTDX_BUILD_CPP_TESTS=ON
#   -DCSTDX_BUILD_FUZZ=ON  (Clang, -fsanitize=fuzzer)

include(${CMAKE_CURRENT_LIST_DIR}/tests-c.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tests-headers.cmake)

if(CSTDX_BUILD_CPP_TESTS)
  include(${CMAKE_CURRENT_LIST_DIR}/tests-cpp.cmake)
endif()

if(CSTDX_BUILD_FUZZ)
  include(${CMAKE_CURRENT_LIST_DIR}/tests-fuzz.cmake)
endif()
