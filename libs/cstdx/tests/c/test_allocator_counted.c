#include <cstdx/allocator_canary.h>
#include <cstdx/allocator_counted.h>
#include <cstdx/checked.h>
#include <stdint.h>

#include "cx_test.h"
#include "cx_test_allocators.h"

typedef struct {
    size_t count;
    size_t bytes;
} leak_totals;

static bool visit_leak(void* context, const void* block, size_t size, size_t alignment) {
    auto totals = (leak_totals*)context;

    totals->count++;
    totals->bytes += size;
    CX_CHECK(block != nullptr);
    CX_CHECK(cx_is_power_of_two(alignment));
    return true;
}

static int test_counting(void) {
    cx_allocator_counted counted;

    CX_CHECK(cx_allocator_counted_init(&counted, nullptr) == CX_OK);
    CX_CHECK(counted.inner == cx_allocator_default());

    auto allocator = cx_allocator_counted_as_allocator(&counted);

    CX_CHECK(cx_allocator_is_valid(&allocator));
    CX_CHECK_EQ(0, counted.allocations);
    CX_CHECK(!cx_allocator_counted_has_leaks(&counted));

    auto a = cx_allocator_alloc(&allocator, 32, 8);
    auto b = cx_allocator_alloc(&allocator, 64, 16);

    CX_CHECK(a != nullptr);
    CX_CHECK(b != nullptr);
    CX_CHECK(((uintptr_t)a % 8) == 0);
    CX_CHECK(((uintptr_t)b % 16) == 0);
    CX_CHECK_EQ(2, counted.allocations);
    CX_CHECK_EQ(2, counted.live_blocks);
    CX_CHECK_EQ(96, counted.live_bytes);
    CX_CHECK_EQ(96, counted.peak_bytes);

    cx_allocator_free(&allocator, a, 32, 8);
    CX_CHECK_EQ(1, counted.releases);
    CX_CHECK_EQ(1, counted.live_blocks);
    CX_CHECK_EQ(64, counted.live_bytes);

    /* The decorator has no realloc callback, so the generic wrapper uses
     * allocate/copy/free and the counters follow that path. */
    b = cx_allocator_realloc(&allocator, b, 64, 128, 16);
    CX_CHECK(b != nullptr);
    CX_CHECK_EQ(3, counted.allocations);
    CX_CHECK_EQ(2, counted.releases);
    CX_CHECK_EQ(1, counted.live_blocks);
    CX_CHECK_EQ(128, counted.live_bytes);
    CX_CHECK_EQ(192, counted.peak_bytes);

    cx_allocator_free(&allocator, b, 128, 16);
    CX_CHECK_EQ(3, counted.releases);
    CX_CHECK_EQ(0, counted.live_blocks);
    CX_CHECK_EQ(0, counted.live_bytes);
    CX_CHECK(!cx_allocator_counted_has_leaks(&counted));

    auto zeroed = (unsigned char*)cx_allocator_alloc_zeroed(&allocator, 16, 8);

    CX_CHECK(zeroed != nullptr);
    if (zeroed != nullptr) {
        auto all_zero = true;

        for (size_t i = 0; i < 16; ++i) {
            if (zeroed[i] != 0) {
                all_zero = false;
            }
        }

        CX_CHECK(all_zero);
    }
    cx_allocator_free(&allocator, zeroed, 16, 8);
    CX_CHECK_EQ(0, counted.live_blocks);

    cx_allocator_counted_destroy(&counted);
    CX_CHECK(counted.inner == nullptr);

    return cx_test_finish("allocator_counted.counting");
}

static int test_leaks(void) {
    cx_allocator_counted counted;

    CX_CHECK(cx_allocator_counted_init(&counted, nullptr) == CX_OK);

    auto allocator = cx_allocator_counted_as_allocator(&counted);
    auto first = cx_allocator_alloc(&allocator, 8, 8);
    auto second = cx_allocator_alloc(&allocator, 16, 8);

    CX_CHECK(first != nullptr);
    CX_CHECK(second != nullptr);
    CX_CHECK(cx_allocator_counted_has_leaks(&counted));

    leak_totals totals = {};

    CX_CHECK_EQ(2, cx_allocator_counted_visit_leaks(&counted, visit_leak, &totals));
    CX_CHECK_EQ(2, totals.count);
    CX_CHECK_EQ(24, totals.bytes);

    cx_allocator_free(&allocator, first, 8, 8);
    cx_allocator_free(&allocator, second, 16, 8);
    CX_CHECK(!cx_allocator_counted_has_leaks(&counted));

    cx_allocator_counted_destroy(&counted);

    return cx_test_finish("allocator_counted.leaks");
}

static int test_compose_with_canary(void) {
    cx_allocator_counted counted;
    cx_allocator_canary canary;

    CX_CHECK(cx_allocator_counted_init(&counted, nullptr) == CX_OK);

    auto counted_view = cx_allocator_counted_as_allocator(&counted);

    CX_CHECK(cx_allocator_canary_init(&canary, &counted_view) == CX_OK);

    auto allocator = cx_allocator_canary_as_allocator(&canary);
    auto block = cx_allocator_alloc(&allocator, 64, 32);

    CX_CHECK(block != nullptr);
    CX_CHECK_EQ(CX_OK, cx_allocator_canary_check(&canary, block));

    cx_allocator_free(&allocator, block, 64, 32);
    CX_CHECK_EQ(0, canary.live_blocks);
    CX_CHECK_EQ(0, counted.live_blocks);
    CX_CHECK(!cx_allocator_counted_has_leaks(&counted));

    cx_allocator_canary_destroy(&canary);
    cx_allocator_counted_destroy(&counted);

    return cx_test_finish("allocator_counted.compose");
}

static bool visit_stop(void* context, const void* block, size_t size, size_t alignment) {
    (void)block;
    (void)size;
    (void)alignment;
    ++*(int*)context;
    return false;
}

static int test_visit_edges(void) {
    cx_allocator_counted counted;

    CX_CHECK(cx_allocator_counted_init(&counted, nullptr) == CX_OK);

    auto allocator = cx_allocator_counted_as_allocator(&counted);
    auto first = cx_allocator_alloc(&allocator, 8, 8);
    auto second = cx_allocator_alloc(&allocator, 8, 8);

    CX_CHECK(first != nullptr);
    CX_CHECK(second != nullptr);

    /* Null arguments visit nothing. */
    CX_CHECK_EQ(0, cx_allocator_counted_visit_leaks(nullptr, visit_leak, nullptr));
    CX_CHECK_EQ(0, cx_allocator_counted_visit_leaks(&counted, nullptr, nullptr));

    /* A callback returning false stops after the first block. */
    int seen = 0;

    CX_CHECK_EQ(1, cx_allocator_counted_visit_leaks(&counted, visit_stop, &seen));
    CX_CHECK_EQ(1, seen);

    cx_allocator_free(&allocator, first, 8, 8);
    cx_allocator_free(&allocator, second, 8, 8);
    cx_allocator_counted_destroy(&counted);

    return cx_test_finish("allocator_counted.visit_edges");
}

static int test_failures_and_lifecycle(void) {
    cx_allocator_counted counted;
    cx_allocator invalid = {0};

    CX_CHECK(cx_allocator_counted_init(nullptr, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_allocator_counted_init(&counted, &invalid) == CX_INVALID_ARGUMENT);

    cx_allocator_failing failing = {
        .fail_at = 1,
        .fail_all_after = false,
    };
    cx_allocator failing_view = cx_allocator_failing_as_allocator(&failing);

    CX_CHECK(cx_allocator_counted_init(&counted, &failing_view) == CX_OK);

    auto allocator = cx_allocator_counted_as_allocator(&counted);

    /* The inner allocator refuses the block itself. */
    CX_CHECK(cx_allocator_alloc(&allocator, 16, 8) == nullptr);
    CX_CHECK_EQ(1, counted.failed_allocations);
    CX_CHECK_EQ(0, counted.allocations);
    CX_CHECK_EQ(0, counted.live_blocks);

    /* The block succeeds but its tracking record cannot be allocated. */
    failing.attempts = 0;
    failing.fail_at = 2;
    CX_CHECK(cx_allocator_alloc(&allocator, 16, 8) == nullptr);
    CX_CHECK_EQ(2, counted.failed_allocations);
    CX_CHECK_EQ(0, counted.allocations);
    CX_CHECK_EQ(0, counted.live_blocks);

    failing.fail_at = 0;

    /* Freeing null is a no-op; an untracked pointer is only forwarded.
     * Allocate it through the same allocator family so the default
     * allocator's platform-specific release routine matches. */
    cx_allocator_free(&allocator, nullptr, 16, 8);
    CX_CHECK_EQ(0, counted.releases);

    void* raw = cx_allocator_alloc(cx_allocator_default(), 16, 8);

    CX_CHECK(raw != nullptr);
    cx_allocator_free(&allocator, raw, 16, 8);
    CX_CHECK_EQ(0, counted.releases);
    CX_CHECK_EQ(0, counted.live_blocks);

    /* Destroying with outstanding blocks releases only their records. */
    auto tracked = cx_allocator_alloc(&allocator, 24, 8);

    CX_CHECK(tracked != nullptr);
    CX_CHECK_EQ(1, counted.live_blocks);

    cx_allocator_counted_destroy(&counted);
    CX_CHECK(counted.inner == nullptr);
    CX_CHECK_EQ(0, counted.live_blocks);
    CX_CHECK(counted.blocks == nullptr);

    cx_allocator_free(cx_allocator_default(), tracked, 24, 8);

    /* Destroying never-initialized or null state is harmless. */
    cx_allocator_counted_destroy(nullptr);

    cx_allocator_counted zeroed = {0};

    cx_allocator_counted_destroy(&zeroed);

    return cx_test_finish("allocator_counted.failures");
}

int main(void) {
    int failures = 0;

    failures += test_counting();
    failures += test_leaks();
    failures += test_compose_with_canary();
    failures += test_visit_edges();
    failures += test_failures_and_lifecycle();

    return failures == 0 ? 0 : 1;
}
