# caudio unit tests.
#
# Each test is a single translation unit with its own main(). Tests link
# against ca_runtime::ca_runtime and are registered with CTest under the
# "unit" label.

set(CAUDIO_UNIT_TESTS
    fs
    path
    time
    mutex
    cond
    rwlock
    tls
    thread
)

foreach(_test IN LISTS CAUDIO_UNIT_TESTS)
    add_executable(caudio_test_${_test} test_${_test}.c)
    target_link_libraries(caudio_test_${_test} PRIVATE ca_runtime::ca_runtime)
    target_include_directories(caudio_test_${_test}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_test(NAME caudio.${_test} COMMAND caudio_test_${_test})
    set_tests_properties(caudio.${_test} PROPERTIES LABELS unit)
endforeach()

unset(CAUDIO_UNIT_TESTS)
