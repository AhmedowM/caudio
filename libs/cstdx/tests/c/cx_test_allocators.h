#ifndef CSTDX_TEST_ALLOCATORS_H
#define CSTDX_TEST_ALLOCATORS_H

/**
 * @file cx_test_allocators.h
 * @brief Test-only allocator decorators (failing and null).
 *
 * These live in the test tree, not in the library: they exist to drive error
 * paths and to prove that a code region allocates nothing.
 */

#include <cstdx/allocator.h>

/**
 * @brief Failing allocator state.
 *
 * `fail_at` is the one-based allocation attempt that fails; @c 0 disables
 * failures. When `fail_all_after` is true, every attempt at or after
 * `fail_at` fails; otherwise only the exact attempt fails.
 */
typedef struct cx_allocator_failing {
    const cx_allocator* inner;
    size_t fail_at;
    size_t attempts;
    bool fail_all_after;
} cx_allocator_failing;

typedef struct cx_allocator_null {
    /** Unused; keeps the state pointer non-null. */
    unsigned char unused;
} cx_allocator_null;

static inline void* cx_allocator_failing_alloc(void* state, size_t size, size_t alignment) {
    auto failing = (cx_allocator_failing*)state;
    failing->attempts++;

    auto failed = false;

    if (failing->fail_at != 0) {
        failed = failing->fail_all_after ? failing->attempts >= failing->fail_at
                                         : failing->attempts == failing->fail_at;
    }

    if (failed) {
        return nullptr;
    }

    return cx_allocator_alloc(failing->inner, size, alignment);
}

static inline void cx_allocator_failing_free(void* state, void* ptr, size_t size,
                                             size_t alignment) {
    auto failing = (cx_allocator_failing*)state;

    cx_allocator_free(failing->inner, ptr, size, alignment);
}

static inline cx_allocator cx_allocator_failing_as_allocator(cx_allocator_failing* failing) {
    if (failing->inner == nullptr) {
        failing->inner = cx_allocator_default();
    }

    return (cx_allocator){
        .state = failing,
        .alloc = cx_allocator_failing_alloc,
        .realloc = nullptr,
        .free = cx_allocator_failing_free,
    };
}

static inline void* cx_allocator_null_alloc(void* state, [[maybe_unused]] size_t size,
                                            [[maybe_unused]] size_t alignment) {
    (void)state;
    return nullptr;
}

static inline void cx_allocator_null_free(void* state, [[maybe_unused]] void* ptr,
                                          [[maybe_unused]] size_t size,
                                          [[maybe_unused]] size_t alignment) {
    (void)state;
}

static inline cx_allocator cx_allocator_null_as_allocator(cx_allocator_null* allocator) {
    return (cx_allocator){
        .state = allocator,
        .alloc = cx_allocator_null_alloc,
        .realloc = nullptr,
        .free = cx_allocator_null_free,
    };
}

#endif /* CSTDX_TEST_ALLOCATORS_H */
