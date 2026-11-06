#include <cstdx/allocator_counted.h>
#include <cstdx/bytes.h>

#include <string.h>

#include "cx_test.h"

static int test_lifecycle(void) {
    cx_bytes *bytes = nullptr;

    CX_CHECK(cx_bytes_init(&bytes, nullptr) == CX_OK);
    CX_CHECK(bytes != nullptr);
    CX_CHECK(cx_bytes_empty(bytes));
    CX_CHECK(cx_bytes_is_null(bytes));
    CX_CHECK_EQ(0, cx_bytes_length(bytes));
    CX_CHECK_EQ(0, cx_bytes_capacity(bytes));
    CX_CHECK(cx_bytes_data(bytes) == nullptr);
    CX_CHECK(cx_bytes_as_slice(bytes).length == 0);
    CX_CHECK(cx_bytes_equals(nullptr, nullptr));
    CX_CHECK(cx_bytes_equals(bytes, nullptr) == false);

    uint8_t data[] = {1, 2, 3};

    CX_CHECK(cx_bytes_assign(bytes, data, sizeof(data)) == CX_OK);
    CX_CHECK(!cx_bytes_empty(bytes));
    CX_CHECK(!cx_bytes_is_null(bytes));
    CX_CHECK_EQ(3, cx_bytes_length(bytes));
    CX_CHECK(cx_bytes_capacity(bytes) >= 3);
    CX_CHECK(memcmp(cx_bytes_data(bytes), data, sizeof(data)) == 0);

    CX_CHECK(cx_bytes_at(bytes, 0) == cx_bytes_data(bytes));
    CX_CHECK_EQ(2, *cx_bytes_at(bytes, 1));
    CX_CHECK(cx_bytes_at(bytes, 3) == nullptr);
    CX_CHECK(cx_bytes_at_mut(bytes, 2) != nullptr);
    CX_CHECK(cx_bytes_at(nullptr, 0) == nullptr);

    uint8_t value = 0;

    CX_CHECK(cx_bytes_get(bytes, 0, &value) == CX_OK);
    CX_CHECK_EQ(1, value);
    CX_CHECK(cx_bytes_get(bytes, 3, &value) == CX_OUT_OF_RANGE);
    CX_CHECK(cx_bytes_get(nullptr, 0, &value) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_bytes_get(bytes, 0, nullptr) == CX_INVALID_ARGUMENT);

    CX_CHECK(cx_bytes_set(bytes, 0, 9) == CX_OK);
    CX_CHECK_EQ(9, *cx_bytes_at(bytes, 0));
    CX_CHECK(cx_bytes_set(bytes, 3, 9) == CX_OUT_OF_RANGE);

    cx_bytes_clear(bytes);
    CX_CHECK(cx_bytes_empty(bytes));
    CX_CHECK(cx_bytes_capacity(bytes) >= 3);

    cx_bytes_destroy(&bytes);
    CX_CHECK(bytes == nullptr);
    cx_bytes_destroy(&bytes);

    return cx_test_finish("bytes.lifecycle");
}

static int test_from_and_copy(void) {
    uint8_t data[] = {4, 5, 6};
    cx_bytes *a = nullptr;
    cx_bytes *b = nullptr;

    CX_CHECK(cx_bytes_from(&a, data, sizeof(data), nullptr) == CX_OK);
    CX_CHECK(cx_bytes_from_slice(&b, cx_slice_from_parts(data, 2), nullptr) ==
             CX_OK);
    CX_CHECK_EQ(3, cx_bytes_length(a));
    CX_CHECK_EQ(2, cx_bytes_length(b));
    CX_CHECK_EQ(4, *cx_bytes_at(a, 0));
    CX_CHECK_EQ(5, *cx_bytes_at(b, 1));

    cx_bytes *copy = nullptr;

    CX_CHECK(cx_bytes_copy(&copy, a) == CX_OK);
    CX_CHECK(cx_bytes_equals(copy, a));
    CX_CHECK(cx_bytes_data(copy) != cx_bytes_data(a));

    cx_bytes *moved = nullptr;

    CX_CHECK(cx_bytes_init(&moved, nullptr) == CX_OK);
    cx_bytes_move(moved, copy);
    CX_CHECK(cx_bytes_equals(moved, a));
    CX_CHECK(cx_bytes_is_null(copy));

    cx_bytes_swap(a, b);
    CX_CHECK_EQ(2, cx_bytes_length(a));
    CX_CHECK_EQ(3, cx_bytes_length(b));

    cx_bytes_destroy(&a);
    cx_bytes_destroy(&b);
    cx_bytes_destroy(&moved);
    cx_bytes_destroy(&copy);

    return cx_test_finish("bytes.from_and_copy");
}

static int test_mutation(void) {
    uint8_t seed[] = {1, 2};
    uint8_t tail[] = {3, 4};
    cx_bytes *bytes = nullptr;

    CX_CHECK(cx_bytes_from(&bytes, seed, sizeof(seed), nullptr) == CX_OK);
    CX_CHECK(cx_bytes_append(bytes, tail, sizeof(tail)) == CX_OK);
    CX_CHECK_EQ(4, cx_bytes_length(bytes));
    CX_CHECK(cx_bytes_push_back(bytes, 5) == CX_OK);
    CX_CHECK_EQ(5, cx_bytes_length(bytes));
    CX_CHECK_EQ(5, *cx_bytes_at(bytes, 4));

    uint8_t removed = 0;

    CX_CHECK(cx_bytes_pop_back(bytes, &removed) == CX_OK);
    CX_CHECK_EQ(5, removed);
    CX_CHECK_EQ(4, cx_bytes_length(bytes));
    CX_CHECK(cx_bytes_pop_back(bytes, nullptr) == CX_OK);
    CX_CHECK(cx_bytes_pop_back(bytes, nullptr) == CX_OK);
    CX_CHECK(cx_bytes_pop_back(bytes, nullptr) == CX_OK);
    CX_CHECK(cx_bytes_pop_back(bytes, nullptr) == CX_OK);
    CX_CHECK(cx_bytes_empty(bytes));
    CX_CHECK(cx_bytes_pop_back(bytes, nullptr) == CX_OUT_OF_RANGE);

    uint8_t abc[] = {'a', 'b', 'c'};

    CX_CHECK(cx_bytes_assign(bytes, abc, sizeof(abc)) == CX_OK);
    CX_CHECK(cx_bytes_reserve(bytes, 32) == CX_OK);
    CX_CHECK(cx_bytes_capacity(bytes) >= 32);
    CX_CHECK(cx_bytes_resize(bytes, 8) == CX_OK);
    CX_CHECK_EQ(8, cx_bytes_length(bytes));
    CX_CHECK_EQ(0, *cx_bytes_at(bytes, 3));
    CX_CHECK_EQ(0, *cx_bytes_at(bytes, 7));
    CX_CHECK(cx_bytes_resize(bytes, 2) == CX_OK);
    CX_CHECK_EQ(2, cx_bytes_length(bytes));
    CX_CHECK_EQ('a', *cx_bytes_at(bytes, 0));
    CX_CHECK_EQ('b', *cx_bytes_at(bytes, 1));

    cx_bytes_destroy(&bytes);

    return cx_test_finish("bytes.mutation");
}

static int test_insert_erase_replace(void) {
    uint8_t hello[] = {'h', 'e', 'l', 'l', 'o'};
    uint8_t world[] = {' ', 'w', 'o', 'r', 'l', 'd'};
    uint8_t prefix[] = {'>', '>', ' '};
    uint8_t there[] = {'t', 'h', 'e', 'r', 'e'};
    uint8_t comma[] = {','};
    cx_bytes *bytes = nullptr;

    CX_CHECK(cx_bytes_from(&bytes, hello, sizeof(hello), nullptr) == CX_OK);
    CX_CHECK(cx_bytes_insert(bytes, 5, world, sizeof(world)) == CX_OK);
    CX_CHECK_EQ(11, cx_bytes_length(bytes));
    CX_CHECK(memcmp(cx_bytes_data(bytes), "hello world", 11) == 0);

    CX_CHECK(cx_bytes_insert(bytes, 0, prefix, sizeof(prefix)) == CX_OK);
    CX_CHECK(memcmp(cx_bytes_data(bytes), ">> hello world", 14) == 0);

    CX_CHECK(cx_bytes_erase(bytes, 0, 3) == CX_OK);
    CX_CHECK(memcmp(cx_bytes_data(bytes), "hello world", 11) == 0);

    CX_CHECK(cx_bytes_replace(bytes, 6, 5, there, sizeof(there)) == CX_OK);
    CX_CHECK(memcmp(cx_bytes_data(bytes), "hello there", 11) == 0);

    CX_CHECK(cx_bytes_replace(bytes, 5, 0, comma, sizeof(comma)) == CX_OK);
    CX_CHECK(memcmp(cx_bytes_data(bytes), "hello, there", 12) == 0);

    CX_CHECK(cx_bytes_erase(bytes, 5, 100) == CX_OK);
    CX_CHECK_EQ(5, cx_bytes_length(bytes));
    CX_CHECK(memcmp(cx_bytes_data(bytes), "hello", 5) == 0);

    uint8_t x = 'x';

    CX_CHECK(cx_bytes_insert(bytes, 6, &x, 1) == CX_OUT_OF_RANGE);
    CX_CHECK(cx_bytes_erase(bytes, 6, 1) == CX_OUT_OF_RANGE);
    CX_CHECK(cx_bytes_replace(bytes, 6, 0, &x, 1) == CX_OUT_OF_RANGE);

    cx_bytes_destroy(&bytes);

    return cx_test_finish("bytes.insert_erase_replace");
}

static int test_aliasing(void) {
    uint8_t data[] = {1, 2, 3, 4, 5, 6};
    cx_bytes *bytes = nullptr;

    CX_CHECK(cx_bytes_from(&bytes, data, sizeof(data), nullptr) == CX_OK);

    cx_slice tail;

    CX_CHECK(cx_slice_sub(cx_bytes_as_slice(bytes), 3, 3, &tail) == CX_OK);
    CX_CHECK(cx_bytes_assign_slice(bytes, tail) == CX_OK);
    CX_CHECK_EQ(3, cx_bytes_length(bytes));
    CX_CHECK_EQ(4, *cx_bytes_at(bytes, 0));
    CX_CHECK_EQ(6, *cx_bytes_at(bytes, 2));

    uint8_t abc[] = {'a', 'b', 'c'};

    CX_CHECK(cx_bytes_assign(bytes, abc, sizeof(abc)) == CX_OK);
    CX_CHECK(cx_bytes_append_slice(bytes, cx_bytes_as_slice(bytes)) == CX_OK);
    CX_CHECK_EQ(6, cx_bytes_length(bytes));
    CX_CHECK(memcmp(cx_bytes_data(bytes), "abcabc", 6) == 0);

    CX_CHECK(cx_bytes_insert(bytes, 3, cx_bytes_data(bytes), 3) == CX_OK);
    CX_CHECK_EQ(9, cx_bytes_length(bytes));
    CX_CHECK(memcmp(cx_bytes_data(bytes), "abcabcabc", 9) == 0);

    CX_CHECK(cx_bytes_assign(bytes, abc, sizeof(abc)) == CX_OK);
    CX_CHECK(cx_bytes_replace(bytes, 0, 1, cx_bytes_data(bytes) + 1, 2) ==
             CX_OK);
    CX_CHECK_EQ(4, cx_bytes_length(bytes));
    CX_CHECK(memcmp(cx_bytes_data(bytes), "bcbc", 4) == 0);

    cx_bytes_destroy(&bytes);

    return cx_test_finish("bytes.aliasing");
}

static int test_slice_interop(void) {
    uint8_t data[] = {10, 20, 30, 40};
    auto slice = cx_slice_from_parts(data, sizeof(data));
    cx_bytes *bytes = nullptr;

    CX_CHECK(cx_bytes_from_slice(&bytes, slice, nullptr) == CX_OK);

    auto borrowed = cx_bytes_as_slice(bytes);

    CX_CHECK_EQ(4, borrowed.length);
    CX_CHECK(borrowed.data == cx_bytes_data(bytes));
    CX_CHECK(cx_slice_equals(borrowed, slice));

    CX_CHECK(cx_bytes_append_slice(bytes, slice) == CX_OK);
    CX_CHECK_EQ(8, cx_bytes_length(bytes));

    cx_bytes_destroy(&bytes);

    return cx_test_finish("bytes.slice_interop");
}

static int test_counted_allocator(void) {
    cx_allocator_counted counted;

    CX_CHECK(cx_allocator_counted_init(&counted, nullptr) == CX_OK);

    auto allocator = cx_allocator_counted_as_allocator(&counted);
    uint8_t data[] = {1, 2, 3, 4};
    cx_bytes *first = nullptr;
    cx_bytes *second = nullptr;

    CX_CHECK(cx_bytes_from(&first, data, sizeof(data), &allocator) == CX_OK);
    CX_CHECK(cx_bytes_copy(&second, first) == CX_OK);

    for (int i = 0; i < 100; ++i) {
        CX_CHECK(cx_bytes_append(first, data, sizeof(data)) == CX_OK);
    }

    CX_CHECK_EQ(4 + 100 * 4, cx_bytes_length(first));
    cx_bytes_destroy(&first);
    cx_bytes_destroy(&second);
    CX_CHECK(!cx_allocator_counted_has_leaks(&counted));

    cx_allocator_counted_destroy(&counted);

    return cx_test_finish("bytes.counted_allocator");
}

int main(void) {
    int failures = 0;

    failures += test_lifecycle();
    failures += test_from_and_copy();
    failures += test_mutation();
    failures += test_insert_erase_replace();
    failures += test_aliasing();
    failures += test_slice_interop();
    failures += test_counted_allocator();

    return failures == 0 ? 0 : 1;
}
