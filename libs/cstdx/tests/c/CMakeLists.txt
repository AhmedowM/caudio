# C unit tests.
#
# Each test is a single translation unit with its own main(). Tests link
# against cstdx::cstdx and are registered with CTest under the "unit" label.

set(CSTDX_UNIT_TESTS
    checked
    status
    allocator
    allocator_counted
    allocator_canary
    allocator_faults
    arena
    pool
    string
    string_view
    string_builder
    slice
    bytes
    vector
    hash
    sort
    base64
)

foreach(_test IN LISTS CSTDX_UNIT_TESTS)
    add_executable(cstdx_test_${_test} test_${_test}.c)
    target_link_libraries(cstdx_test_${_test} PRIVATE cstdx::cstdx)
    target_include_directories(cstdx_test_${_test}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(CSTDX_ENABLE_SANITIZERS)
        target_compile_options(cstdx_test_${_test} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all -Werror)
        target_link_options(cstdx_test_${_test} PRIVATE -fsanitize=address,undefined)
    endif()

    add_test(NAME cstdx.${_test} COMMAND cstdx_test_${_test})
    set_tests_properties(cstdx.${_test} PROPERTIES LABELS unit)
endforeach()

unset(CSTDX_UNIT_TESTS)
