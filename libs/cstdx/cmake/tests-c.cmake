# C unit tests.
#
# Each tests/c/test_*.c file is one executable with its own main(). Adding a
# test file is enough: CONFIGURE_DEPENDS re-runs configure and registers it.
# Tests link against cstdx::cstdx and are registered under the "unit" label.

file(GLOB _cstdx_test_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/tests/c/test_*.c"
)

foreach(_source IN LISTS _cstdx_test_sources)
    # test_base64.c -> base64
    get_filename_component(_test "${_source}" NAME_WE)
    string(REGEX REPLACE "^test_" "" _test "${_test}")

    add_executable(cstdx_test_${_test} "${_source}")
    target_link_libraries(cstdx_test_${_test} PRIVATE cstdx::cstdx)
    target_include_directories(cstdx_test_${_test}
        PRIVATE "${PROJECT_SOURCE_DIR}/tests/c"
    )

    add_test(NAME cstdx.${_test} COMMAND cstdx_test_${_test})
    set_tests_properties(cstdx.${_test} PROPERTIES LABELS unit)
endforeach()

unset(_cstdx_test_sources)
unset(_source)
unset(_test)
