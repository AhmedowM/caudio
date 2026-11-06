# cstdx tests
#
# The C suite runs everywhere. The C++23 differential oracle and the libFuzzer
# targets are opt-in because they need extra toolchains/dependencies:
#   -DCSTDX_BUILD_CPP_TESTS=ON
#   -DCSTDX_BUILD_FUZZ=ON  (Clang, -fsanitize=fuzzer)

add_subdirectory(c)
add_subdirectory(headers)

if(CSTDX_BUILD_CPP_TESTS)
  add_subdirectory(cpp)
endif()

if(CSTDX_BUILD_FUZZ)
  add_subdirectory(fuzz)
endif()
