# Public-header self-containment smoke targets.
#
# `headers.c` is always built. `headers.cpp` is built when a C++ compiler is
# available, because the C++ differential harness includes the same headers.

set(_cstdx_headers_dir ${PROJECT_SOURCE_DIR}/tests/headers)

# Keep the header smoke binaries in a predictable directory: the CI headers job
# runs build/tests/headers/cstdx_headers_{c,cpp} directly. The per-config
# variants are set as well so multi-config generators land in the same place
# instead of appending a configuration subdirectory.
set(_cstdx_headers_out ${CMAKE_BINARY_DIR}/tests/headers)

add_executable(cstdx_headers_c ${_cstdx_headers_dir}/headers.c)
target_link_libraries(cstdx_headers_c PRIVATE cstdx::cstdx)
set_target_properties(cstdx_headers_c PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${_cstdx_headers_out}"
)
foreach(_config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(cstdx_headers_c PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_${_config} "${_cstdx_headers_out}"
  )
endforeach()

add_test(NAME cstdx.headers.c COMMAND cstdx_headers_c)
set_tests_properties(cstdx.headers.c PROPERTIES LABELS headers)

include(CheckLanguage)
check_language(CXX)
if(CMAKE_CXX_COMPILER)
  enable_language(CXX)

  add_executable(cstdx_headers_cpp ${_cstdx_headers_dir}/headers.cpp)
  target_link_libraries(cstdx_headers_cpp PRIVATE cstdx::cstdx)
  target_compile_features(cstdx_headers_cpp PRIVATE cxx_std_23)
  set_target_properties(cstdx_headers_cpp PROPERTIES
      CXX_STANDARD 23
      CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF
      RUNTIME_OUTPUT_DIRECTORY "${_cstdx_headers_out}"
  )
  foreach(_config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(cstdx_headers_cpp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_config} "${_cstdx_headers_out}"
    )
  endforeach()

  add_test(NAME cstdx.headers.cpp COMMAND cstdx_headers_cpp)
  set_tests_properties(cstdx.headers.cpp PROPERTIES LABELS headers)
endif()

unset(_cstdx_headers_dir)
unset(_cstdx_headers_out)
unset(_config)
