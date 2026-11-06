#include <cstdx/arena.h>

#include <stdint.h>
#include <string.h>

#include "cx_test.h"

static constexpr size_t arena_buffer_size = 256;

static int test_basic(void) {
    cx_arena arena;

    CX_CHECK(cx_arena_init(&arena, nullptr) == CX_OK);
    CX_CHECK_EQ(0, cx_arena_bytes_used(&arena));
    CX_CHECK_EQ(0, cx_arena_bytes_reserved(&arena));

    auto values =
        (int *)cx_arena_alloc(&arena, sizeof(int) * 4, alignof(int));

    CX_CHECK(values != nullptr);
    if (values != nullptr) {
        for (int i = 0; i < 4; ++i) {
            values[i] = i * i;
        }
        CX_CHECK_EQ(9, values[3]);
    }

    CX_CHECK(cx_arena_bytes_used(&arena) >= sizeof(int) * 4);
    CX_CHECK(cx_arena_bytes_reserved(&arena) > 0);
    CX_CHECK(cx_arena_alloc(&arena, 0, 0) == nullptr);

    auto overaligned = cx_arena_alloc(&arena, 1, 64);

    CX_CHECK(overaligned != nullptr);
    CX_CHECK(((uintptr_t)overaligned % 64) == 0);

    auto zeroed = (unsigned char *)cx_arena_alloc_zeroed(&arena, 64, 8);

    CX_CHECK(zeroed != nullptr);
    if (zeroed != nullptr) {
        auto all_zero = true;

        for (size_t i = 0; i < 64; ++i) {
            if (zeroed[i] != 0) {
                all_zero = false;
            }
        }

        CX_CHECK(all_zero);
    }

    auto reserved = cx_arena_bytes_reserved(&arena);

    cx_arena_reset(&arena);
    CX_CHECK_EQ(0, cx_arena_bytes_used(&arena));
    CX_CHECK_EQ(reserved, cx_arena_bytes_reserved(&arena));

    auto again = cx_arena_alloc(&arena, 16, 8);
    CX_CHECK(again != nullptr);

    cx_arena_destroy(&arena);
    CX_CHECK(arena.current == nullptr);

    return cx_test_finish("arena.basic");
}

static int test_realloc(void) {
    cx_arena arena;

    CX_CHECK(cx_arena_init(&arena, nullptr) == CX_OK);

    auto last = (char *)cx_arena_alloc(&arena, 8, 1);

    CX_CHECK(last != nullptr);
    if (last != nullptr) {
        memcpy(last, "abcdefg", 7);
    }

    auto used_before = cx_arena_bytes_used(&arena);
    auto grown = cx_arena_realloc(&arena, last, 8, 16, 1);

    CX_CHECK(grown == last);
    CX_CHECK_EQ(used_before + 8, cx_arena_bytes_used(&arena));
    if (grown != nullptr) {
        CX_CHECK(memcmp(grown, "abcdefg", 7) == 0);
    }

    auto shrunk = cx_arena_realloc(&arena, grown, 16, 4, 1);

    CX_CHECK(shrunk == grown);
    CX_CHECK_EQ(4, cx_arena_bytes_used(&arena));
    if (shrunk != nullptr) {
        CX_CHECK(memcmp(shrunk, "abcd", 4) == 0);
    }

    CX_CHECK(cx_arena_realloc(&arena, shrunk, 4, 0, 1) == nullptr);

    /* A block that is not the most recent one cannot grow in place. */
    auto first = (char *)cx_arena_alloc(&arena, 8, 1);
    auto second = (char *)cx_arena_alloc(&arena, 8, 1);

    CX_CHECK(first != nullptr);
    CX_CHECK(second != nullptr);
    if (first != nullptr) {
        memcpy(first, "0123456", 7);
    }

    auto moved = cx_arena_realloc(&arena, first, 8, 32, 1);

    CX_CHECK(moved != nullptr);
    CX_CHECK(moved != first);
    if (moved != nullptr) {
        CX_CHECK(memcmp(moved, "0123456", 7) == 0);
    }

    cx_arena_destroy(&arena);

    return cx_test_finish("arena.realloc");
}

static int test_static(void) {
    unsigned char buffer[arena_buffer_size];
    cx_arena arena;

    CX_CHECK(cx_arena_init_static(&arena, buffer, sizeof(buffer)) == CX_OK);
    CX_CHECK_EQ(sizeof(buffer), cx_arena_bytes_reserved(&arena));

    auto block = (unsigned char *)cx_arena_alloc(&arena, 100, 1);

    CX_CHECK(block != nullptr);
    CX_CHECK((uintptr_t)block >= (uintptr_t)buffer);
    CX_CHECK((uintptr_t)block < (uintptr_t)buffer + sizeof(buffer));

    CX_CHECK(cx_arena_alloc(&arena, sizeof(buffer), 1) == nullptr);

    cx_arena_reset(&arena);
    CX_CHECK_EQ(0, cx_arena_bytes_used(&arena));
    CX_CHECK_EQ(sizeof(buffer), cx_arena_bytes_reserved(&arena));

    block = (unsigned char *)cx_arena_alloc(&arena, 100, 1);
    CX_CHECK(block != nullptr);

    cx_arena_destroy(&arena);
    CX_CHECK(arena.current == nullptr);

    /* The caller owns the buffer and it must still be usable. */
    buffer[0] = 1;
    CX_CHECK_EQ(1, buffer[0]);

    return cx_test_finish("arena.static");
}

static int test_mark_rewind(void) {
    cx_arena arena;

    CX_CHECK(cx_arena_init(&arena, nullptr) == CX_OK);

    auto first = (char *)cx_arena_alloc(&arena, 16, 1);

    CX_CHECK(first != nullptr);

    auto used_before_mark = cx_arena_bytes_used(&arena);
    auto checkpoint = cx_arena_mark(&arena);

    auto temporary = (char *)cx_arena_alloc(&arena, 16, 1);

    CX_CHECK(temporary != nullptr);
    CX_CHECK(cx_arena_bytes_used(&arena) > used_before_mark);

    cx_arena_rewind(&arena, checkpoint);
    CX_CHECK_EQ(used_before_mark, cx_arena_bytes_used(&arena));

    auto reused = (char *)cx_arena_alloc(&arena, 16, 1);

    CX_CHECK(reused == temporary);

    /* Rewinding across a chunk boundary releases the extra chunk. */
    auto boundary = cx_arena_mark(&arena);
    auto large = cx_arena_alloc(&arena, 8192, 1);

    CX_CHECK(large != nullptr);

    auto reserved_with_extra = cx_arena_bytes_reserved(&arena);

    cx_arena_rewind(&arena, boundary);
    CX_CHECK(cx_arena_bytes_reserved(&arena) < reserved_with_extra);

    cx_arena_destroy(&arena);

    return cx_test_finish("arena.mark_rewind");
}

static int test_as_allocator(void) {
    cx_arena arena;

    CX_CHECK(cx_arena_init(&arena, nullptr) == CX_OK);

    auto view = cx_arena_as_allocator(&arena);

    CX_CHECK(cx_allocator_is_valid(&view));
    CX_CHECK(view.alloc != nullptr);
    CX_CHECK(view.realloc != nullptr);
    CX_CHECK(view.free != nullptr);

    auto block = cx_allocator_alloc(&view, 32, 16);

    CX_CHECK(block != nullptr);
    CX_CHECK(((uintptr_t)block % 16) == 0);
    CX_CHECK(cx_arena_bytes_used(&arena) >= 32);

    auto resized = cx_allocator_realloc(&view, block, 32, 64, 16);

    CX_CHECK(resized != nullptr);
    CX_CHECK(cx_arena_bytes_used(&arena) >= 64);

    /* The view's free is a no-op: arenas do not reclaim individuals. */
    auto used_before_free = cx_arena_bytes_used(&arena);

    cx_allocator_free(&view, resized, 64, 16);
    CX_CHECK_EQ(used_before_free, cx_arena_bytes_used(&arena));

    cx_arena_destroy(&arena);

    return cx_test_finish("arena.as_allocator");
}

int main(void) {
    int failures = 0;

    failures += test_basic();
    failures += test_realloc();
    failures += test_static();
    failures += test_mark_rewind();
    failures += test_as_allocator();

    return failures == 0 ? 0 : 1;
}
