#include <cstdx/pool.h>
#include <stdint.h>

#include "cx_test.h"
#include "cx_test_allocators.h"

static constexpr size_t pool_test_slot_count = 8;
static constexpr size_t pool_test_buffer_size = 256;
static constexpr size_t pool_test_slot_bytes = 1024;

static int test_basic(void) {
    cx_pool pool;

    CX_CHECK(cx_pool_init(&pool, 16, 8, 4, nullptr) == CX_OK);
    CX_CHECK_EQ(0, cx_pool_live(&pool));
    CX_CHECK_EQ(0, cx_pool_capacity(&pool));

    void* slots[pool_test_slot_count] = {};

    for (size_t i = 0; i < pool_test_slot_count; ++i) {
        slots[i] = cx_pool_alloc(&pool);
        CX_CHECK(slots[i] != nullptr);
        CX_CHECK(((uintptr_t)slots[i] % 8) == 0);
    }

    CX_CHECK_EQ(pool_test_slot_count, cx_pool_live(&pool));
    CX_CHECK_EQ(pool_test_slot_count, cx_pool_capacity(&pool));

    for (size_t i = 0; i < pool_test_slot_count; ++i) {
        for (size_t j = i + 1; j < pool_test_slot_count; ++j) {
            CX_CHECK(slots[i] != slots[j]);
        }
    }

    cx_pool_free(&pool, slots[3]);
    CX_CHECK_EQ(pool_test_slot_count - 1, cx_pool_live(&pool));

    auto reused = cx_pool_alloc(&pool);

    CX_CHECK(reused == slots[3]);
    CX_CHECK_EQ(pool_test_slot_count, cx_pool_live(&pool));

    cx_pool_reset(&pool);
    CX_CHECK_EQ(0, cx_pool_live(&pool));
    CX_CHECK_EQ(4, cx_pool_capacity(&pool));

    auto after_reset = cx_pool_alloc(&pool);

    CX_CHECK(after_reset != nullptr);
    CX_CHECK_EQ(1, cx_pool_live(&pool));

    cx_pool_free(&pool, nullptr);
    CX_CHECK_EQ(1, cx_pool_live(&pool));

    cx_pool_destroy(&pool);
    CX_CHECK(pool.current == nullptr);

    return cx_test_finish("pool.basic");
}

static int test_slot_rounding(void) {
    cx_pool pool;

    CX_CHECK(cx_pool_init(&pool, 3, 8, 4, nullptr) == CX_OK);
    CX_CHECK_EQ(8, cx_pool_slot_size(&pool));
    cx_pool_destroy(&pool);

    CX_CHECK(cx_pool_init(&pool, 1, 1, 4, nullptr) == CX_OK);
    CX_CHECK(cx_pool_slot_size(&pool) >= sizeof(void*));
    cx_pool_destroy(&pool);

    CX_CHECK(cx_pool_init(nullptr, 8, 8, 4, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_pool_init(&pool, 0, 8, 4, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_pool_init(&pool, 8, 3, 4, nullptr) == CX_INVALID_ARGUMENT);

    return cx_test_finish("pool.rounding");
}

static int test_static(void) {
    alignas(16) unsigned char buffer[pool_test_buffer_size];
    cx_pool pool;

    CX_CHECK(cx_pool_init_static(&pool, buffer, sizeof(buffer), 32, 16) == CX_OK);
    CX_CHECK_EQ(8, cx_pool_capacity(&pool));

    void* slots[pool_test_slot_count] = {};

    for (size_t i = 0; i < pool_test_slot_count; ++i) {
        slots[i] = cx_pool_alloc(&pool);
        CX_CHECK(slots[i] != nullptr);
    }

    CX_CHECK_EQ(pool_test_slot_count, cx_pool_live(&pool));
    CX_CHECK(cx_pool_alloc(&pool) == nullptr);

    cx_pool_free(&pool, slots[0]);
    CX_CHECK(cx_pool_alloc(&pool) == slots[0]);

    cx_pool_reset(&pool);
    CX_CHECK_EQ(0, cx_pool_live(&pool));
    CX_CHECK_EQ(pool_test_slot_count, cx_pool_capacity(&pool));

    auto again = cx_pool_alloc(&pool);

    CX_CHECK(again != nullptr);

    cx_pool_destroy(&pool);
    CX_CHECK(pool.current == nullptr);

    /* The caller owns the buffer and it must still be usable. */
    buffer[0] = 1;
    CX_CHECK_EQ(1, buffer[0]);

    return cx_test_finish("pool.static");
}

static int test_large_slots(void) {
    cx_pool pool;

    CX_CHECK(cx_pool_init(&pool, pool_test_slot_bytes, 0, 2, nullptr) == CX_OK);
    CX_CHECK_EQ(pool_test_slot_bytes, cx_pool_slot_size(&pool));

    auto first = cx_pool_alloc(&pool);
    auto second = cx_pool_alloc(&pool);
    auto third = cx_pool_alloc(&pool);

    CX_CHECK(first != nullptr);
    CX_CHECK(second != nullptr);
    CX_CHECK(third != nullptr);
    CX_CHECK_EQ(4, cx_pool_capacity(&pool));

    cx_pool_free(&pool, first);
    cx_pool_free(&pool, second);
    cx_pool_free(&pool, third);
    CX_CHECK_EQ(0, cx_pool_live(&pool));

    cx_pool_destroy(&pool);

    return cx_test_finish("pool.large_slots");
}

static int test_errors(void) {
    cx_pool pool;
    cx_allocator invalid = {0};
    unsigned char buffer[64];
    alignas(8) unsigned char tiny[8];

    CX_CHECK(cx_pool_init(&pool, 8, 8, 4, &invalid) == CX_INVALID_ARGUMENT);

    /* Rounding the slot size overflows. */
    CX_CHECK(cx_pool_init(&pool, SIZE_MAX, 8, 4, nullptr) == CX_OVERFLOW);

    cx_pool_destroy(nullptr);
    cx_pool_reset(nullptr);

    CX_CHECK(cx_pool_alloc(nullptr) == nullptr);
    CX_CHECK_EQ(0, cx_pool_live(nullptr));
    CX_CHECK_EQ(0, cx_pool_capacity(nullptr));
    CX_CHECK_EQ(0, cx_pool_slot_size(nullptr));

    CX_CHECK(cx_pool_init_static(nullptr, buffer, sizeof(buffer), 8, 8) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_pool_init_static(&pool, nullptr, sizeof(buffer), 8, 8) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_pool_init_static(&pool, buffer, 0, 8, 8) == CX_INVALID_ARGUMENT);

    /* The aligned start lies past the end of the supplied buffer. */
    CX_CHECK(cx_pool_init_static(&pool, buffer + 1, 1, 8, 16) == CX_INVALID_ARGUMENT);

    /* The buffer is too small to hold even one slot. */
    CX_CHECK(cx_pool_init_static(&pool, tiny, sizeof(tiny), 64, 8) == CX_INVALID_ARGUMENT);

    /* Reset before the first chunk was ever allocated. */
    CX_CHECK(cx_pool_init(&pool, 8, 8, 4, nullptr) == CX_OK);
    cx_pool_reset(&pool);
    CX_CHECK_EQ(0, cx_pool_live(&pool));
    CX_CHECK_EQ(0, cx_pool_capacity(&pool));
    cx_pool_destroy(&pool);

    /* Growing a chunk whose slot count overflows the byte count fails. */
    CX_CHECK(cx_pool_init(&pool, 16, 8, SIZE_MAX, nullptr) == CX_OK);
    CX_CHECK(cx_pool_alloc(&pool) == nullptr);
    cx_pool_destroy(&pool);

    return cx_test_finish("pool.errors");
}

static int test_allocator_failures(void) {
    cx_allocator_failing failing = {
        .fail_at = 1,
        .fail_all_after = false,
    };
    cx_allocator allocator = cx_allocator_failing_as_allocator(&failing);
    cx_pool pool;

    CX_CHECK(cx_pool_init(&pool, 8, 8, 1, &allocator) == CX_OK);

    /* The first chunk's backing block cannot be allocated. */
    CX_CHECK(cx_pool_alloc(&pool) == nullptr);
    CX_CHECK_EQ(0, cx_pool_capacity(&pool));

    failing.fail_at = 0;
    CX_CHECK(cx_pool_alloc(&pool) != nullptr);
    CX_CHECK_EQ(1, cx_pool_capacity(&pool));
    CX_CHECK_EQ(1, cx_pool_live(&pool));

    /* The chunk header fails after its backing block was allocated, so the
     * block must be released again. */
    failing.fail_at = failing.attempts + 2;
    CX_CHECK(cx_pool_alloc(&pool) == nullptr);
    CX_CHECK_EQ(1, cx_pool_capacity(&pool));
    CX_CHECK_EQ(1, cx_pool_live(&pool));

    failing.fail_at = 0;
    CX_CHECK(cx_pool_alloc(&pool) != nullptr);
    CX_CHECK_EQ(2, cx_pool_capacity(&pool));
    CX_CHECK_EQ(2, cx_pool_live(&pool));

    cx_pool_destroy(&pool);

    return cx_test_finish("pool.failures");
}

int main(void) {
    int failures = 0;

    failures += test_basic();
    failures += test_slot_rounding();
    failures += test_static();
    failures += test_large_slots();
    failures += test_errors();
    failures += test_allocator_failures();

    return failures == 0 ? 0 : 1;
}
