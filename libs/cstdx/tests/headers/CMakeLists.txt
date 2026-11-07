# Public-header self-containment smoke targets.
#
# `headers.c` is always built. `headers.cpp` is built when a C++ compiler is
# available, because the C++ differential harness includes the same headers.

add_executable(cstdx_headers_c headers.c)
target_link_libraries(cstdx_headers_c PRIVATE cstdx::cstdx)

if(CSTDX_ENABLE_SANITIZERS)
  target_compile_options(cstdx_headers_c PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all -Werror)
  target_link_options(cstdx_headers_c PRIVATE -fsanitize=address,undefined)
endif()

add_test(NAME cstdx.headers.c COMMAND cstdx_headers_c)
set_tests_properties(cstdx.headers.c PROPERTIES LABELS headers)

include(CheckLanguage)
check_language(CXX)
if(CMAKE_CXX_COMPILER)
  enable_language(CXX)

  add_executable(cstdx_headers_cpp headers.cpp)
  target_link_libraries(cstdx_headers_cpp PRIVATE cstdx::cstdx)
  target_compile_features(cstdx_headers_cpp PRIVATE cxx_std_23)
  set_target_properties(cstdx_headers_cpp PROPERTIES
      CXX_STANDARD 23
      CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF
  )

  if(CSTDX_ENABLE_SANITIZERS)
    target_compile_options(cstdx_headers_cpp PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all -Werror)
    target_link_options(cstdx_headers_cpp PRIVATE -fsanitize=address,undefined)
  endif()

  add_test(NAME cstdx.headers.cpp COMMAND cstdx_headers_cpp)
  set_tests_properties(cstdx.headers.cpp PROPERTIES LABELS headers)
endif()
