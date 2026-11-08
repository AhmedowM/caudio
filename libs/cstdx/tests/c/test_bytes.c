#include <cstdx/allocator_counted.h>
#include <cstdx/bytes.h>
#include <stdint.h>
#include <string.h>

#include "cx_test.h"
#include "cx_test_allocators.h"

static int test_lifecycle(void) {
    cx_bytes* bytes = nullptr;

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
    cx_bytes* a = nullptr;
    cx_bytes* b = nullptr;

    CX_CHECK(cx_bytes_from(&a, data, sizeof(data), nullptr) == CX_OK);
    CX_CHECK(cx_bytes_from_slice(&b, cx_slice_from_parts(data, 2), nullptr) == CX_OK);
    CX_CHECK_EQ(3, cx_bytes_length(a));
    CX_CHECK_EQ(2, cx_bytes_length(b));
    CX_CHECK_EQ(4, *cx_bytes_at(a, 0));
    CX_CHECK_EQ(5, *cx_bytes_at(b, 1));

    cx_bytes* copy = nullptr;

    CX_CHECK(cx_bytes_copy(&copy, a) == CX_OK);
    CX_CHECK(cx_bytes_equals(copy, a));
    CX_CHECK(cx_bytes_data(copy) != cx_bytes_data(a));

    cx_bytes* moved = nullptr;

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
    cx_bytes* bytes = nullptr;

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
    cx_bytes* bytes = nullptr;

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
    cx_bytes* bytes = nullptr;

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
    CX_CHECK(cx_bytes_replace(bytes, 0, 1, cx_bytes_data(bytes) + 1, 2) == CX_OK);
    CX_CHECK_EQ(4, cx_bytes_length(bytes));
    CX_CHECK(memcmp(cx_bytes_data(bytes), "bcbc", 4) == 0);

    cx_bytes_destroy(&bytes);

    return cx_test_finish("bytes.aliasing");
}

static int test_slice_interop(void) {
    uint8_t data[] = {10, 20, 30, 40};
    auto slice = cx_slice_from_parts(data, sizeof(data));
    cx_bytes* bytes = nullptr;

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
    cx_bytes* first = nullptr;
    cx_bytes* second = nullptr;

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

static int test_invalid_arguments(void) {
    cx_bytes* bytes = nullptr;
    cx_allocator invalid = {0};

    CX_CHECK(cx_bytes_init(nullptr, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_bytes_init(&bytes, &invalid) == CX_INVALID_ARGUMENT);
    CX_CHECK(bytes == nullptr);

    CX_CHECK(cx_bytes_from(nullptr, "x", 1, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_bytes_from(&bytes, nullptr, 1, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_bytes_copy(nullptr, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_bytes_copy(&bytes, nullptr) == CX_INVALID_ARGUMENT);

    CX_CHECK(cx_bytes_assign(nullptr, "x", 1) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_bytes_append(nullptr, "x", 1) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_bytes_push_back(nullptr, 0) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_bytes_pop_back(nullptr, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_bytes_insert(nullptr, 0, "x", 1) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_bytes_erase(nullptr, 0, 1) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_bytes_replace(nullptr, 0, 0, "x", 1) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_bytes_resize(nullptr, 1) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_bytes_reserve(nullptr, 1) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_bytes_set(nullptr, 0, 0) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_bytes_at_mut(nullptr, 0) == nullptr);
    CX_CHECK(cx_bytes_as_slice(nullptr).data == nullptr);
    CX_CHECK_EQ(0, cx_bytes_as_slice(nullptr).length);

    cx_bytes_clear(nullptr);
    cx_bytes_move(nullptr, nullptr);
    cx_bytes_swap(nullptr, nullptr);
    cx_bytes_destroy(nullptr);
    cx_bytes_destroy(&bytes);

    return cx_test_finish("bytes.invalid_arguments");
}

static int test_empty_operations(void) {
    cx_bytes* bytes = nullptr;

    CX_CHECK(cx_bytes_init(&bytes, nullptr) == CX_OK);

    CX_CHECK(cx_bytes_append(bytes, nullptr, 0) == CX_OK);
    CX_CHECK(cx_bytes_insert(bytes, 0, nullptr, 0) == CX_OK);
    CX_CHECK(cx_bytes_erase(bytes, 0, 0) == CX_OK);
    CX_CHECK(cx_bytes_replace(bytes, 0, 0, nullptr, 0) == CX_OK);
    CX_CHECK(cx_bytes_resize(bytes, 0) == CX_OK);
    CX_CHECK(cx_bytes_pop_back(bytes, nullptr) == CX_OUT_OF_RANGE);

    uint8_t data[] = {1, 2};

    CX_CHECK(cx_bytes_assign(bytes, data, sizeof(data)) == CX_OK);

    /* A count past the end is clamped to the remaining length. */
    CX_CHECK(cx_bytes_replace(bytes, 0, 99, data, 2) == CX_OK);
    CX_CHECK_EQ(2, cx_bytes_length(bytes));
    CX_CHECK(cx_bytes_replace(bytes, 2, 99, nullptr, 0) == CX_OK);
    CX_CHECK(cx_bytes_erase(bytes, 2, 99) == CX_OK);
    CX_CHECK_EQ(2, cx_bytes_length(bytes));

    cx_bytes_swap(bytes, bytes);
    cx_bytes_destroy(&bytes);

    return cx_test_finish("bytes.empty_operations");
}

static int test_allocation_failures(void) {
    cx_allocator_failing failing = {
        .fail_at = 1,
        .fail_all_after = false,
    };
    cx_allocator allocator = cx_allocator_failing_as_allocator(&failing);
    cx_bytes* bytes = nullptr;

    CX_CHECK(cx_bytes_init(&bytes, &allocator) == CX_OUT_OF_MEMORY);
    CX_CHECK(bytes == nullptr);
    CX_CHECK_EQ(1, failing.attempts);

    failing.fail_at = 2;
    failing.attempts = 0;

    CX_CHECK(cx_bytes_init(&bytes, &allocator) == CX_OK);

    CX_CHECK(cx_bytes_reserve(bytes, 16) == CX_OUT_OF_MEMORY);
    CX_CHECK(cx_bytes_data(bytes) == nullptr);
    CX_CHECK_EQ(0, cx_bytes_capacity(bytes));

    /* Only the exact attempt failed, so this one succeeds. */
    CX_CHECK(cx_bytes_reserve(bytes, 16) == CX_OK);
    CX_CHECK(cx_bytes_capacity(bytes) >= 16);

    uint8_t data[] = {1, 2, 3, 4};

    CX_CHECK(cx_bytes_assign(bytes, data, sizeof(data)) == CX_OK);

    /* Appending an aliased range stages a copy first; that copy fails. */
    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_bytes_append(bytes, cx_bytes_data(bytes), 2) == CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(4, cx_bytes_length(bytes));

    uint8_t large[64] = {0};

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_bytes_assign(bytes, large, sizeof(large)) == CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(4, cx_bytes_length(bytes));

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_bytes_insert(bytes, 0, large, 16) == CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(4, cx_bytes_length(bytes));

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_bytes_resize(bytes, 32) == CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(4, cx_bytes_length(bytes));

    CX_CHECK(cx_bytes_resize(bytes, 16) == CX_OK);

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_bytes_push_back(bytes, 7) == CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(16, cx_bytes_length(bytes));

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_bytes_replace(bytes, 0, 0, large, 32) == CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(16, cx_bytes_length(bytes));

    /* cx_bytes_from reports the handle allocation failure directly. */
    failing.fail_at = failing.attempts + 1;

    cx_bytes* from = nullptr;

    CX_CHECK(cx_bytes_from(&from, data, sizeof(data), &allocator) == CX_OUT_OF_MEMORY);
    CX_CHECK(from == nullptr);

    /* It also frees the handle when the assignment that follows fails. */
    failing.fail_at = failing.attempts + 2;

    CX_CHECK(cx_bytes_from(&from, large, 32, &allocator) == CX_OUT_OF_MEMORY);
    CX_CHECK(from == nullptr);

    failing.fail_at = 0;

    cx_bytes* copy = nullptr;

    CX_CHECK(cx_bytes_copy(&copy, bytes) == CX_OK);
    CX_CHECK_EQ(16, cx_bytes_length(copy));

    cx_bytes_destroy(&copy);
    cx_bytes_destroy(&bytes);

    return cx_test_finish("bytes.failures");
}

static int test_move(void) {
    cx_bytes* left = nullptr;
    cx_bytes* right = nullptr;
    uint8_t data[] = {1, 2, 3};

    CX_CHECK(cx_bytes_init(&left, nullptr) == CX_OK);
    CX_CHECK(cx_bytes_init(&right, nullptr) == CX_OK);
    CX_CHECK(cx_bytes_assign(left, data, sizeof(data)) == CX_OK);
    CX_CHECK(cx_bytes_assign(right, data, 1) == CX_OK);

    /* Moving into a non-empty destination releases its buffer first. */
    cx_bytes_move(left, right);
    CX_CHECK_EQ(1, cx_bytes_length(left));
    CX_CHECK_EQ(0, cx_bytes_length(right));

    cx_bytes_move(left, left);
    cx_bytes_move(left, nullptr);
    cx_bytes_swap(left, left);

    cx_bytes_destroy(&right);
    cx_bytes_destroy(&left);

    return cx_test_finish("bytes.move");
}

static int test_equality_edges(void) {
    cx_bytes* empty = nullptr;
    cx_bytes* other_empty = nullptr;
    cx_bytes* three = nullptr;
    uint8_t abc[] = {1, 2, 3};

    CX_CHECK(cx_bytes_init(&empty, nullptr) == CX_OK);
    CX_CHECK(cx_bytes_init(&other_empty, nullptr) == CX_OK);
    CX_CHECK(cx_bytes_from(&three, abc, sizeof(abc), nullptr) == CX_OK);

    /* Distinct empty buffers compare equal without touching their data. */
    CX_CHECK(cx_bytes_equals(empty, other_empty));
    CX_CHECK(cx_bytes_equals(empty, empty));

    /* Different lengths compare unequal. */
    CX_CHECK(!cx_bytes_equals(empty, three));
    CX_CHECK(!cx_bytes_equals(three, empty));

    cx_bytes_destroy(&empty);
    cx_bytes_destroy(&other_empty);
    cx_bytes_destroy(&three);

    return cx_test_finish("bytes.equality_edges");
}

static int test_reserve_overflow(void) {
    cx_bytes* bytes = nullptr;

    CX_CHECK(cx_bytes_init(&bytes, nullptr) == CX_OK);

    /* Doubling the capacity eventually overflows, so growth falls back to the
     * requested minimum, which is too large to allocate. */
    CX_CHECK(cx_bytes_reserve(bytes, SIZE_MAX) == CX_OUT_OF_MEMORY);
    CX_CHECK(cx_bytes_data(bytes) == nullptr);
    CX_CHECK_EQ(0, cx_bytes_capacity(bytes));

    cx_bytes_destroy(&bytes);

    return cx_test_finish("bytes.reserve_overflow");
}

static int test_staged_failures(void) {
    cx_allocator_failing failing = {
        .fail_at = 0,
        .fail_all_after = false,
    };
    cx_allocator allocator = cx_allocator_failing_as_allocator(&failing);
    uint8_t data[] = {1, 2, 3, 4};
    cx_bytes* bytes = nullptr;

    CX_CHECK(cx_bytes_from(&bytes, data, sizeof(data), &allocator) == CX_OK);

    /* Assigning an aliased range stages a detached copy, which fails here. */
    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_bytes_assign(bytes, cx_bytes_data(bytes), 2) == CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(4, cx_bytes_length(bytes));
    CX_CHECK(memcmp(cx_bytes_data(bytes), data, sizeof(data)) == 0);

    /* The same staging step fails for insert and replace. */
    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_bytes_insert(bytes, 0, cx_bytes_data(bytes), 2) == CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(4, cx_bytes_length(bytes));

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_bytes_replace(bytes, 0, 1, cx_bytes_data(bytes), 2) == CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(4, cx_bytes_length(bytes));
    CX_CHECK(memcmp(cx_bytes_data(bytes), data, sizeof(data)) == 0);

    failing.fail_at = 0;
    cx_bytes_destroy(&bytes);

    return cx_test_finish("bytes.staged_failures");
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
    failures += test_invalid_arguments();
    failures += test_empty_operations();
    failures += test_allocation_failures();
    failures += test_move();
    failures += test_equality_edges();
    failures += test_reserve_overflow();
    failures += test_staged_failures();

    return failures == 0 ? 0 : 1;
}
