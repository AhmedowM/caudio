#include <cstdx/allocator.h>
#include <stdint.h>
#include <string.h>

#include "cx_test.h"

typedef struct {
    size_t allocations;
    size_t releases;
} counters;

static void* counting_alloc(void* state, size_t size, size_t alignment) {
    auto counters_state = (counters*)state;
    auto block = cx_allocator_alloc(nullptr, size, alignment);

    if (block != nullptr) {
        counters_state->allocations++;
    }

    return block;
}

static void counting_free(void* state, void* ptr, size_t size, size_t alignment) {
    auto counters_state = (counters*)state;

    if (ptr != nullptr) {
        counters_state->releases++;
    }

    cx_allocator_free(nullptr, ptr, size, alignment);
}

static int test_default_allocator(void) {
    auto allocator = cx_allocator_default();

    CX_CHECK(allocator != nullptr);
    CX_CHECK(cx_allocator_is_valid(allocator));
    CX_CHECK(cx_allocator_is_valid(nullptr));

    auto block = (unsigned char*)cx_allocator_alloc(allocator, 32, 0);

    CX_CHECK(block != nullptr);
    if (block != nullptr) {
        memset(block, 0xAB, 32);
        CX_CHECK_EQ(0xAB, block[31]);
    }
    cx_allocator_free(allocator, block, 32, 0);

    CX_CHECK(cx_allocator_alloc(allocator, 0, 8) == nullptr);
    CX_CHECK(cx_allocator_alloc(allocator, 8, 3) == nullptr);

    auto overaligned = cx_allocator_alloc(allocator, 64, 64);

    CX_CHECK(overaligned != nullptr);
    CX_CHECK(((uintptr_t)overaligned % 64) == 0);
    cx_allocator_free(allocator, overaligned, 64, 64);

    auto zeroed = (unsigned char*)cx_allocator_alloc_zeroed(allocator, 16, 0);

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
    cx_allocator_free(allocator, zeroed, 16, 0);

    return cx_test_finish("allocator.default");
}

static int test_realloc(void) {
    auto allocator = cx_allocator_default();
    auto block = (unsigned char*)cx_allocator_alloc(allocator, 8, 0);

    CX_CHECK(block != nullptr);
    if (block != nullptr) {
        memcpy(block, "abcdefg", 8);
    }

    block = cx_allocator_realloc(allocator, block, 8, 64, 0);
    CX_CHECK(block != nullptr);
    if (block != nullptr) {
        CX_CHECK(memcmp(block, "abcdefg", 7) == 0);
    }

    block = cx_allocator_realloc(allocator, block, 64, 4, 0);
    CX_CHECK(block != nullptr);
    if (block != nullptr) {
        CX_CHECK(memcmp(block, "abcd", 4) == 0);
    }

    block = cx_allocator_realloc(allocator, block, 4, 0, 0);
    CX_CHECK(block == nullptr);

    auto fresh = cx_allocator_realloc(allocator, nullptr, 0, 32, 16);
    CX_CHECK(fresh != nullptr);
    cx_allocator_free(allocator, fresh, 32, 16);

    return cx_test_finish("allocator.realloc");
}

static int test_custom_allocator(void) {
    counters state = {};
    auto allocator = (cx_allocator){
        .state = &state,
        .alloc = counting_alloc,
        .realloc = nullptr,
        .free = counting_free,
    };

    CX_CHECK(cx_allocator_is_valid(&allocator));

    auto block = cx_allocator_alloc(&allocator, 10, 0);

    CX_CHECK(block != nullptr);
    CX_CHECK_EQ(1, state.allocations);

    auto resized = cx_allocator_realloc(&allocator, block, 10, 20, 0);

    CX_CHECK(resized != nullptr);
    CX_CHECK_EQ(2, state.allocations);
    CX_CHECK_EQ(1, state.releases);

    cx_allocator_free(&allocator, resized, 20, 0);
    CX_CHECK_EQ(2, state.releases);

    cx_allocator_free(&allocator, nullptr, 0, 0);

    return cx_test_finish("allocator.custom");
}

int main(void) {
    int failures = 0;

    failures += test_default_allocator();
    failures += test_realloc();
    failures += test_custom_allocator();

    return failures == 0 ? 0 : 1;
}
