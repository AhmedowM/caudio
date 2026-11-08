#include <cstdx/allocator_canary.h>
#include <cstdx/allocator_counted.h>
#include <cstdx/arena.h>

#include "cx_test.h"
#include "cx_test_allocators.h"

static int test_null_allocator(void) {
    cx_allocator_null null_allocator = {};
    auto allocator = cx_allocator_null_as_allocator(&null_allocator);

    CX_CHECK(cx_allocator_is_valid(&allocator));
    CX_CHECK(cx_allocator_alloc(&allocator, 16, 8) == nullptr);
    CX_CHECK(cx_allocator_alloc_zeroed(&allocator, 16, 8) == nullptr);
    CX_CHECK(cx_allocator_realloc(&allocator, nullptr, 0, 16, 8) == nullptr);
    cx_allocator_free(&allocator, nullptr, 0, 0);

    return cx_test_finish("allocator_faults.null");
}

static int test_failing_allocator(void) {
    cx_allocator_failing failing = {
        .fail_at = 1,
        .fail_all_after = false,
    };
    auto allocator = cx_allocator_failing_as_allocator(&failing);
    cx_arena arena;

    CX_CHECK(cx_arena_init(&arena, &allocator) == CX_OK);
    CX_CHECK(cx_arena_alloc(&arena, 16, 8) == nullptr);
    CX_CHECK_EQ(1, failing.attempts);

    /* Only the first attempt had to fail, so the next one succeeds. */
    auto block = cx_arena_alloc(&arena, 16, 8);

    CX_CHECK(block != nullptr);
    CX_CHECK_EQ(2, failing.attempts);
    cx_arena_destroy(&arena);

    return cx_test_finish("allocator_faults.failing");
}

static int test_failing_init_with_capacity(void) {
    cx_allocator_failing failing = {
        .fail_at = 1,
        .fail_all_after = true,
    };
    auto allocator = cx_allocator_failing_as_allocator(&failing);
    cx_arena arena;

    CX_CHECK(cx_arena_init_with_capacity(&arena, 4'096, &allocator) == CX_OUT_OF_MEMORY);

    return cx_test_finish("allocator_faults.failing_init");
}

static int test_counted_metadata_failure(void) {
    cx_allocator_failing failing = {
        .fail_at = 2,
        .fail_all_after = false,
    };
    auto inner = cx_allocator_failing_as_allocator(&failing);
    cx_allocator_counted counted;

    CX_CHECK(cx_allocator_counted_init(&counted, &inner) == CX_OK);

    auto allocator = cx_allocator_counted_as_allocator(&counted);

    /* The user block succeeds; the tracking record fails and the user block
     * must be released again. */
    CX_CHECK(cx_allocator_alloc(&allocator, 16, 8) == nullptr);
    CX_CHECK_EQ(2, failing.attempts);
    CX_CHECK_EQ(1, counted.failed_allocations);
    CX_CHECK_EQ(0, counted.live_blocks);
    CX_CHECK(!cx_allocator_counted_has_leaks(&counted));

    cx_allocator_counted_destroy(&counted);

    return cx_test_finish("allocator_faults.counted_metadata");
}

static int test_canary_metadata_failure(void) {
    cx_allocator_failing failing = {
        .fail_at = 2,
        .fail_all_after = false,
    };
    auto inner = cx_allocator_failing_as_allocator(&failing);
    cx_allocator_canary canary;

    CX_CHECK(cx_allocator_canary_init(&canary, &inner) == CX_OK);

    auto allocator = cx_allocator_canary_as_allocator(&canary);

    CX_CHECK(cx_allocator_alloc(&allocator, 16, 8) == nullptr);
    CX_CHECK_EQ(2, failing.attempts);
    CX_CHECK_EQ(0, canary.live_blocks);
    CX_CHECK(!cx_allocator_canary_has_corruption(&canary));

    cx_allocator_canary_destroy(&canary);

    return cx_test_finish("allocator_faults.canary_metadata");
}

int main(void) {
    int failures = 0;

    failures += test_null_allocator();
    failures += test_failing_allocator();
    failures += test_failing_init_with_capacity();
    failures += test_counted_metadata_failure();
    failures += test_canary_metadata_failure();

    return failures == 0 ? 0 : 1;
}
