# caudio unit tests.
#
# Each test is a single translation unit with its own main(). Tests link
# against ca_runtime::ca_runtime and are registered with CTest under the
# "unit" label.

set(_caudio_tests_dir ${PROJECT_SOURCE_DIR}/tests/c)

# test_<name>.c -> caudio_test_<name> / caudio.<name>. CONFIGURE_DEPENDS makes
# new test files register automatically.
file(GLOB _caudio_test_sources CONFIGURE_DEPENDS
    "${_caudio_tests_dir}/test_*.c"
)

foreach(_source IN LISTS _caudio_test_sources)
    get_filename_component(_test "${_source}" NAME_WE)
    string(REGEX REPLACE "^test_" "" _test "${_test}")

    add_executable(caudio_test_${_test} "${_source}")
    target_link_libraries(caudio_test_${_test} PRIVATE ca_runtime::ca_runtime)
    target_include_directories(caudio_test_${_test}
        PRIVATE ${_caudio_tests_dir}
    )

    if(WIN32 AND _test STREQUAL "socket")
        target_link_libraries(caudio_test_${_test} PRIVATE ws2_32)
    endif()

    add_test(NAME caudio.${_test} COMMAND caudio_test_${_test})
    set_tests_properties(caudio.${_test} PROPERTIES LABELS unit)
endforeach()

unset(_caudio_tests_dir)
unset(_caudio_test_sources)
unset(_source)
unset(_test)
