#include <cstdx/allocator_canary.h>
#include <stdint.h>
#include <string.h>

#include "cx_test.h"

static int test_guards(void) {
    cx_allocator_canary canary;

    CX_CHECK(cx_allocator_canary_init(&canary, nullptr) == CX_OK);
    CX_CHECK(canary.inner == cx_allocator_default());

    auto allocator = cx_allocator_canary_as_allocator(&canary);
    auto block = (unsigned char*)cx_allocator_alloc(&allocator, 32, 16);

    CX_CHECK(block != nullptr);
    CX_CHECK(((uintptr_t)block % 16) == 0);
    CX_CHECK_EQ(1, canary.live_blocks);
    CX_CHECK_EQ(CX_OK, cx_allocator_canary_check(&canary, block));

    memset(block, 0x5A, 32);
    CX_CHECK_EQ(CX_OK, cx_allocator_canary_check(&canary, block));
    CX_CHECK_EQ(0x5A, block[31]);

    cx_allocator_free(&allocator, block, 32, 16);
    CX_CHECK_EQ(0, canary.live_blocks);
    CX_CHECK(!cx_allocator_canary_has_corruption(&canary));

    cx_allocator_canary_destroy(&canary);
    CX_CHECK(canary.inner == nullptr);

    return cx_test_finish("allocator_canary.guards");
}

static int test_overflow(void) {
    cx_allocator_canary canary;

    CX_CHECK(cx_allocator_canary_init(&canary, nullptr) == CX_OK);

    auto allocator = cx_allocator_canary_as_allocator(&canary);
    auto block = (unsigned char*)cx_allocator_alloc(&allocator, 16, 8);

    CX_CHECK(block != nullptr);
    block[16] = 0xFF;
    CX_CHECK_EQ(CX_INVALID_STATE, cx_allocator_canary_check(&canary, block));

    cx_allocator_free(&allocator, block, 16, 8);
    CX_CHECK(cx_allocator_canary_has_corruption(&canary));
    CX_CHECK_EQ(1, canary.corruptions);

    cx_allocator_canary_destroy(&canary);

    return cx_test_finish("allocator_canary.overflow");
}

static int test_underflow(void) {
    cx_allocator_canary canary;

    CX_CHECK(cx_allocator_canary_init(&canary, nullptr) == CX_OK);

    auto allocator = cx_allocator_canary_as_allocator(&canary);
    auto block = (unsigned char*)cx_allocator_alloc(&allocator, 16, 8);

    CX_CHECK(block != nullptr);
    block[-1] = 0xFF;
    CX_CHECK_EQ(CX_INVALID_STATE, cx_allocator_canary_check(&canary, block));

    cx_allocator_free(&allocator, block, 16, 8);
    CX_CHECK(cx_allocator_canary_has_corruption(&canary));

    cx_allocator_canary_destroy(&canary);

    return cx_test_finish("allocator_canary.underflow");
}

static int test_unknown_block(void) {
    cx_allocator_canary canary;

    CX_CHECK(cx_allocator_canary_init(&canary, nullptr) == CX_OK);

    auto foreign = cx_allocator_alloc(nullptr, 16, 8);

    CX_CHECK(foreign != nullptr);
    CX_CHECK_EQ(CX_NOT_FOUND, cx_allocator_canary_check(&canary, foreign));
    CX_CHECK_EQ(CX_INVALID_ARGUMENT, cx_allocator_canary_check(&canary, nullptr));

    cx_allocator_free(nullptr, foreign, 16, 8);

    cx_allocator_canary_destroy(&canary);

    return cx_test_finish("allocator_canary.unknown_block");
}

static int test_realloc(void) {
    cx_allocator_canary canary;

    CX_CHECK(cx_allocator_canary_init(&canary, nullptr) == CX_OK);

    auto allocator = cx_allocator_canary_as_allocator(&canary);
    auto block = (unsigned char*)cx_allocator_alloc(&allocator, 16, 8);

    CX_CHECK(block != nullptr);
    memcpy(block, "abcdefghijklmno", 16);

    block = cx_allocator_realloc(&allocator, block, 16, 64, 8);
    CX_CHECK(block != nullptr);
    if (block != nullptr) {
        CX_CHECK(memcmp(block, "abcdefghijklmno", 16) == 0);
        CX_CHECK_EQ(CX_OK, cx_allocator_canary_check(&canary, block));
    }

    cx_allocator_free(&allocator, block, 64, 8);
    CX_CHECK_EQ(0, canary.live_blocks);
    CX_CHECK(!cx_allocator_canary_has_corruption(&canary));

    cx_allocator_canary_destroy(&canary);

    return cx_test_finish("allocator_canary.realloc");
}

int main(void) {
    int failures = 0;

    failures += test_guards();
    failures += test_overflow();
    failures += test_underflow();
    failures += test_unknown_block();
    failures += test_realloc();

    return failures == 0 ? 0 : 1;
}
