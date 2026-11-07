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
    process
    socket
    shm
    file_lock
)

foreach(_test IN LISTS CAUDIO_UNIT_TESTS)
    add_executable(caudio_test_${_test} test_${_test}.c)
    target_link_libraries(caudio_test_${_test} PRIVATE ca_runtime::ca_runtime)
    target_include_directories(caudio_test_${_test}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(CSTDX_ENABLE_SANITIZERS)
        target_compile_options(caudio_test_${_test} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all -Werror)
        target_link_options(caudio_test_${_test} PRIVATE -fsanitize=address,undefined)
    endif()

    if(WIN32 AND _test STREQUAL "socket")
        target_link_libraries(caudio_test_${_test} PRIVATE ws2_32)
    endif()

    add_test(NAME caudio.${_test} COMMAND caudio_test_${_test})
    set_tests_properties(caudio.${_test} PROPERTIES LABELS unit)
endforeach()

unset(CAUDIO_UNIT_TESTS)
