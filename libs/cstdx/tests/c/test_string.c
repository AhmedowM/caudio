#include <cstdx/allocator_counted.h>
#include <cstdx/string.h>
#include <stdint.h>
#include <string.h>

#include "cx_test.h"
#include "cx_test_allocators.h"

static int test_lifecycle(void) {
    cx_string* str = nullptr;

    CX_CHECK(cx_string_init(&str, nullptr) == CX_OK);
    CX_CHECK(str != nullptr);
    CX_CHECK(cx_string_empty(str));
    CX_CHECK(cx_string_is_null(str));
    CX_CHECK_EQ(0, cx_string_length(str));
    CX_CHECK(strcmp(cx_string_cstr(str), "") == 0);
    CX_CHECK(strcmp(cx_string_cstr(nullptr), "") == 0);

    CX_CHECK(cx_string_assign(str, "hello") == CX_OK);
    CX_CHECK(!cx_string_empty(str));
    CX_CHECK(!cx_string_is_null(str));
    CX_CHECK_EQ(5, cx_string_length(str));
    CX_CHECK(cx_string_capacity(str) >= 5);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello") == 0);

    CX_CHECK(cx_string_at(str, 1) != nullptr);
    CX_CHECK_EQ('e', *cx_string_at(str, 1));
    CX_CHECK_EQ('o', *cx_string_at_mut(str, 4));
    CX_CHECK(cx_string_at(str, 5) == nullptr);
    CX_CHECK(cx_string_at(nullptr, 0) == nullptr);

    char value = 0;

    CX_CHECK(cx_string_get(str, 0, &value) == CX_OK);
    CX_CHECK_EQ('h', value);
    CX_CHECK(cx_string_get(str, 5, &value) == CX_OUT_OF_RANGE);
    CX_CHECK(cx_string_get(nullptr, 0, &value) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_get(str, 0, nullptr) == CX_INVALID_ARGUMENT);

    CX_CHECK(cx_string_set(str, 0, 'H') == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "Hello") == 0);
    CX_CHECK(cx_string_set(str, 5, 'x') == CX_OUT_OF_RANGE);

    cx_string_clear(str);
    CX_CHECK(cx_string_empty(str));
    CX_CHECK_EQ('\0', cx_string_cstr(str)[0]);

    cx_string_destroy(&str);
    CX_CHECK(str == nullptr);
    cx_string_destroy(&str);

    return cx_test_finish("string.lifecycle");
}

static int test_from_and_copy(void) {
    cx_string* a = nullptr;
    cx_string* b = nullptr;

    CX_CHECK(cx_string_from(&a, "alpha", nullptr) == CX_OK);
    CX_CHECK(cx_string_from_view(&b, cx_string_view_from_cstr("beta"), nullptr) == CX_OK);
    CX_CHECK(cx_string_equals_cstr(a, "alpha"));
    CX_CHECK(cx_string_equals_cstr(b, "beta"));

    cx_string* copy = nullptr;

    CX_CHECK(cx_string_copy(&copy, a) == CX_OK);
    CX_CHECK(cx_string_equals(copy, a));
    CX_CHECK(cx_string_cstr(copy) != cx_string_cstr(a));

    cx_string* moved = nullptr;

    CX_CHECK(cx_string_init(&moved, nullptr) == CX_OK);
    cx_string_move(moved, copy);
    CX_CHECK(cx_string_equals(moved, a));
    CX_CHECK(cx_string_empty(copy));
    CX_CHECK_EQ(0, cx_string_length(copy));

    cx_string_swap(a, b);
    CX_CHECK(cx_string_equals_cstr(a, "beta"));
    CX_CHECK(cx_string_equals_cstr(b, "alpha"));

    cx_string_destroy(&a);
    cx_string_destroy(&b);
    cx_string_destroy(&moved);
    cx_string_destroy(&copy);

    return cx_test_finish("string.from_and_copy");
}

static int test_mutation(void) {
    cx_string* str = nullptr;

    CX_CHECK(cx_string_from(&str, "foo", nullptr) == CX_OK);
    CX_CHECK(cx_string_append_cstr(str, "bar") == CX_OK);
    CX_CHECK(cx_string_push_back(str, '!') == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "foobar!") == 0);

    char removed = 0;

    CX_CHECK(cx_string_pop_back(str, &removed) == CX_OK);
    CX_CHECK_EQ('!', removed);
    CX_CHECK(strcmp(cx_string_cstr(str), "foobar") == 0);
    CX_CHECK(cx_string_pop_back(str, nullptr) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "fooba") == 0);
    CX_CHECK(cx_string_pop_back(str, nullptr) == CX_OK);
    CX_CHECK(cx_string_pop_back(str, nullptr) == CX_OK);
    CX_CHECK(cx_string_pop_back(str, nullptr) == CX_OK);
    CX_CHECK(cx_string_pop_back(str, nullptr) == CX_OK);
    CX_CHECK(cx_string_pop_back(str, nullptr) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "") == 0);
    CX_CHECK(cx_string_pop_back(str, nullptr) == CX_OUT_OF_RANGE);

    CX_CHECK(cx_string_assign(str, "abc") == CX_OK);
    CX_CHECK(cx_string_reserve(str, 32) == CX_OK);
    CX_CHECK(cx_string_capacity(str) >= 32);
    CX_CHECK(cx_string_resize(str, 8) == CX_OK);
    CX_CHECK_EQ(8, cx_string_length(str));
    CX_CHECK_EQ(0, cx_string_cstr(str)[3]);
    CX_CHECK_EQ(0, cx_string_cstr(str)[7]);
    CX_CHECK(cx_string_resize(str, 2) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "ab") == 0);

    cx_string_destroy(&str);

    return cx_test_finish("string.mutation");
}

static int test_insert_erase_replace(void) {
    cx_string* str = nullptr;

    CX_CHECK(cx_string_from(&str, "hello", nullptr) == CX_OK);
    CX_CHECK(cx_string_insert(str, 5, cx_string_view_from_cstr(" world")) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello world") == 0);

    CX_CHECK(cx_string_insert(str, 0, cx_string_view_from_cstr(">> ")) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), ">> hello world") == 0);

    CX_CHECK(cx_string_erase(str, 0, 3) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello world") == 0);

    CX_CHECK(cx_string_replace(str, 6, 5, cx_string_view_from_cstr("there")) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello there") == 0);

    CX_CHECK(cx_string_replace(str, 5, 0, cx_string_view_from_cstr(",")) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello, there") == 0);

    CX_CHECK(cx_string_erase(str, 5, 100) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello") == 0);
    CX_CHECK(cx_string_insert(str, 6, cx_string_view_from_cstr("x")) == CX_OUT_OF_RANGE);
    CX_CHECK(cx_string_erase(str, 6, 1) == CX_OUT_OF_RANGE);
    CX_CHECK(cx_string_replace(str, 6, 0, cx_string_view_from_cstr("x")) == CX_OUT_OF_RANGE);

    cx_string_destroy(&str);

    return cx_test_finish("string.insert_erase_replace");
}

static int test_aliasing(void) {
    cx_string* str = nullptr;

    CX_CHECK(cx_string_from(&str, "hello world", nullptr) == CX_OK);

    cx_string_view tail;

    CX_CHECK(cx_string_view_sub(cx_string_view_of(str), 6, 5, &tail) == CX_OK);
    CX_CHECK(cx_string_assign_view(str, tail) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "world") == 0);

    CX_CHECK(cx_string_assign(str, "abc") == CX_OK);
    CX_CHECK(cx_string_append(str, str) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "abcabc") == 0);

    CX_CHECK(cx_string_insert(str, 3, cx_string_view_of(str)) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "abcabcabcabc") == 0);

    cx_string_view middle;

    CX_CHECK(cx_string_view_sub(cx_string_view_of(str), 3, 6, &middle) == CX_OK);
    CX_CHECK(cx_string_replace(str, 0, 3, middle) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "abcabcabcabcabc") == 0);

    cx_string_destroy(&str);

    return cx_test_finish("string.aliasing");
}

static int test_search_and_compare(void) {
    cx_string* str = nullptr;
    size_t index = 0;

    CX_CHECK(cx_string_from(&str, "hello world", nullptr) == CX_OK);
    CX_CHECK(cx_string_find_cstr(str, "world", &index) == CX_OK);
    CX_CHECK_EQ(6, index);
    CX_CHECK(cx_string_find_cstr(str, "", &index) == CX_OK);
    CX_CHECK_EQ(0, index);
    CX_CHECK(cx_string_find_cstr(str, "xyz", &index) == CX_NOT_FOUND);
    CX_CHECK(cx_string_contains(str, cx_string_view_from_cstr("lo wo")));
    CX_CHECK(!cx_string_contains(str, cx_string_view_from_cstr("xyz")));
    CX_CHECK(cx_string_starts_with(str, cx_string_view_from_cstr("hello")));
    CX_CHECK(!cx_string_starts_with(str, cx_string_view_from_cstr("world")));
    CX_CHECK(cx_string_ends_with(str, cx_string_view_from_cstr("world")));
    CX_CHECK(!cx_string_ends_with(str, cx_string_view_from_cstr("hello")));

    cx_string* other = nullptr;

    CX_CHECK(cx_string_from(&other, "hello worle", nullptr) == CX_OK);
    CX_CHECK_EQ(CX_STRING_LESS, cx_string_compare(str, other));
    CX_CHECK_EQ(CX_STRING_GREATER, cx_string_compare(other, str));
    CX_CHECK_EQ(CX_STRING_EQUAL, cx_string_compare(str, str));
    CX_CHECK_EQ(CX_STRING_LESS, cx_string_compare_cstr(str, "z"));
    CX_CHECK_EQ(CX_STRING_GREATER, cx_string_compare_cstr(str, "a"));

    cx_string_destroy(&str);
    cx_string_destroy(&other);

    return cx_test_finish("string.search_and_compare");
}

static int test_counted_allocator(void) {
    cx_allocator_counted counted;

    CX_CHECK(cx_allocator_counted_init(&counted, nullptr) == CX_OK);

    auto allocator = cx_allocator_counted_as_allocator(&counted);
    cx_string* first = nullptr;
    cx_string* second = nullptr;

    CX_CHECK(cx_string_from(&first, "counted", &allocator) == CX_OK);
    CX_CHECK(cx_string_copy(&second, first) == CX_OK);

    for (int i = 0; i < 100; ++i) {
        CX_CHECK(cx_string_append_cstr(first, "-growth") == CX_OK);
    }

    CX_CHECK(cx_string_equals(first, first));
    cx_string_destroy(&first);
    cx_string_destroy(&second);
    CX_CHECK(!cx_allocator_counted_has_leaks(&counted));

    cx_allocator_counted_destroy(&counted);

    return cx_test_finish("string.counted_allocator");
}

static int test_invalid_arguments(void) {
    cx_string* str = nullptr;
    cx_allocator invalid = {0};
    cx_string_view view = cx_string_view_from_cstr("x");
    size_t index = 0;

    CX_CHECK(cx_string_init(nullptr, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_init(&str, &invalid) == CX_INVALID_ARGUMENT);
    CX_CHECK(str == nullptr);

    CX_CHECK(cx_string_from(nullptr, "x", nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_from(&str, nullptr, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_from_view(nullptr, view, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_from_view(&str, (cx_string_view){nullptr, 1}, nullptr) ==
             CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_copy(nullptr, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_copy(&str, nullptr) == CX_INVALID_ARGUMENT);

    CX_CHECK(cx_string_init(&str, nullptr) == CX_OK);

    CX_CHECK(cx_string_assign(str, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_assign(nullptr, "x") == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_assign_view(nullptr, view) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_assign_view(str, (cx_string_view){nullptr, 1}) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_append(str, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_append_cstr(str, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_append(nullptr, str) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_append_view(nullptr, view) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_append_view(str, (cx_string_view){nullptr, 1}) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_push_back(nullptr, 'x') == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_pop_back(nullptr, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_insert(nullptr, 0, view) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_insert(str, 0, (cx_string_view){nullptr, 1}) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_erase(nullptr, 0, 1) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_replace(nullptr, 0, 0, view) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_replace(str, 0, 0, (cx_string_view){nullptr, 1}) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_set(nullptr, 0, 'x') == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_resize(nullptr, 0) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_reserve(nullptr, 1) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_at_mut(nullptr, 0) == nullptr);
    CX_CHECK(cx_string_at_mut(str, 0) == nullptr);
    CX_CHECK(cx_string_find(nullptr, view, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_string_find_cstr(str, nullptr, &index) == CX_INVALID_ARGUMENT);
    CX_CHECK(!cx_string_contains(nullptr, view));

    cx_string_clear(nullptr);
    cx_string_swap(nullptr, nullptr);
    cx_string_swap(str, str);
    cx_string_move(nullptr, nullptr);
    cx_string_move(str, nullptr);
    cx_string_move(str, str);
    cx_string_destroy(nullptr);

    CX_CHECK(!cx_string_equals(nullptr, str));
    CX_CHECK(!cx_string_equals(str, nullptr));
    CX_CHECK(!cx_string_equals_cstr(nullptr, "x"));
    CX_CHECK(!cx_string_equals_cstr(str, nullptr));
    CX_CHECK_EQ(CX_STRING_LESS, cx_string_compare(nullptr, str));
    CX_CHECK_EQ(CX_STRING_GREATER, cx_string_compare(str, nullptr));
    CX_CHECK_EQ(CX_STRING_EQUAL, cx_string_compare_cstr(nullptr, nullptr));
    CX_CHECK_EQ(CX_STRING_LESS, cx_string_compare_cstr(nullptr, "x"));
    CX_CHECK_EQ(CX_STRING_GREATER, cx_string_compare_cstr(str, nullptr));

    auto empty_view = cx_string_view_of(nullptr);

    CX_CHECK(empty_view.data == nullptr);
    CX_CHECK_EQ(0, empty_view.length);

    cx_string_destroy(&str);
    CX_CHECK(str == nullptr);

    return cx_test_finish("string.invalid_arguments");
}

static int test_empty_operations(void) {
    cx_string* str = nullptr;
    cx_string_view empty = {nullptr, 0};

    CX_CHECK(cx_string_from(&str, "abcd", nullptr) == CX_OK);

    CX_CHECK(cx_string_append_view(str, empty) == CX_OK);
    CX_CHECK(cx_string_insert(str, 2, empty) == CX_OK);
    CX_CHECK(cx_string_erase(str, 4, 0) == CX_OK);
    CX_CHECK(cx_string_replace(str, 4, 0, empty) == CX_OK);
    CX_CHECK(cx_string_replace(str, 4, 0, cx_string_view_from_cstr("!")) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "abcd!") == 0);

    /* A removal count past the end is clamped to the remaining length. */
    CX_CHECK(cx_string_replace(str, 3, 99, cx_string_view_from_cstr("XY")) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "abcXY") == 0);

    cx_string_destroy(&str);

    return cx_test_finish("string.empty_operations");
}

static int test_overflow(void) {
    cx_string* str = nullptr;
    cx_string_view huge = {.data = "x", .length = SIZE_MAX};

    CX_CHECK(cx_string_from(&str, "abc", nullptr) == CX_OK);

    /* Growing past SIZE_MAX reports overflow without touching the string. */
    CX_CHECK(cx_string_resize(str, SIZE_MAX) == CX_OVERFLOW);
    CX_CHECK(cx_string_append_view(str, huge) == CX_OVERFLOW);
    CX_CHECK(cx_string_insert(str, 0, huge) == CX_OVERFLOW);
    CX_CHECK(cx_string_replace(str, 0, 0, huge) == CX_OVERFLOW);
    CX_CHECK(strcmp(cx_string_cstr(str), "abc") == 0);

    cx_string_destroy(&str);

    return cx_test_finish("string.overflow");
}

static int test_lifecycle_edges(void) {
    cx_string* dst = nullptr;
    cx_string* src = nullptr;

    CX_CHECK(cx_string_from(&dst, "keep", nullptr) == CX_OK);
    CX_CHECK(cx_string_from(&src, "move", nullptr) == CX_OK);

    /* Moving onto a non-empty destination releases its old buffer. */
    cx_string_move(dst, src);
    CX_CHECK(cx_string_equals_cstr(dst, "move"));
    CX_CHECK(cx_string_empty(src));
    CX_CHECK(cx_string_is_null(src));

    auto view = cx_string_view_of(dst);

    CX_CHECK_EQ(4, view.length);
    CX_CHECK(memcmp(view.data, "move", 4) == 0);

    cx_string_destroy(&dst);
    cx_string_destroy(&src);
    CX_CHECK(dst == nullptr);
    CX_CHECK(src == nullptr);

    return cx_test_finish("string.lifecycle_edges");
}

static int test_allocation_failures(void) {
    cx_allocator_failing failing = {
        .fail_at = 1,
        .fail_all_after = false,
    };
    cx_allocator allocator = cx_allocator_failing_as_allocator(&failing);
    cx_string* str = nullptr;

    /* Handle allocation fails. */
    CX_CHECK(cx_string_init(&str, &allocator) == CX_OUT_OF_MEMORY);
    CX_CHECK(str == nullptr);

    /* The constructor reports a failed handle allocation. */
    failing.attempts = 0;
    failing.fail_at = 1;
    CX_CHECK(cx_string_from_view(&str, cx_string_view_from_cstr("x"), &allocator) ==
             CX_OUT_OF_MEMORY);
    CX_CHECK(str == nullptr);

    /* The constructor frees the handle when the assignment fails. */
    failing.attempts = 0;
    failing.fail_at = 2;
    CX_CHECK(cx_string_from_view(&str, cx_string_view_from_cstr("abcdef"), &allocator) ==
             CX_OUT_OF_MEMORY);
    CX_CHECK(str == nullptr);

    failing.attempts = 0;
    failing.fail_at = 0;
    CX_CHECK(cx_string_init(&str, &allocator) == CX_OK);

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_string_reserve(str, 64) == CX_OUT_OF_MEMORY);
    CX_CHECK(cx_string_is_null(str));

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_string_assign(str, "hello") == CX_OUT_OF_MEMORY);
    CX_CHECK(cx_string_empty(str));

    failing.fail_at = 0;
    CX_CHECK(cx_string_assign(str, "hello") == CX_OK);

    /* Aliased assignments stage a detached copy before growing. */
    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_string_assign_view(str, cx_string_view_of(str)) == CX_OUT_OF_MEMORY);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello") == 0);

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_string_resize(str, 128) == CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(5, cx_string_length(str));

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_string_append_view(str, cx_string_view_of(str)) == CX_OUT_OF_MEMORY);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello") == 0);

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_string_insert(str, 0, cx_string_view_of(str)) == CX_OUT_OF_MEMORY);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello") == 0);

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_string_replace(str, 0, 1, cx_string_view_of(str)) == CX_OUT_OF_MEMORY);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello") == 0);

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_string_append_cstr(str, "0123456789abcdef") == CX_OUT_OF_MEMORY);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello") == 0);

    CX_CHECK(cx_string_resize(str, 8) == CX_OK);

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_string_push_back(str, 'x') == CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(8, cx_string_length(str));

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_string_insert(str, 0, cx_string_view_from_cstr("0123456789abcdef")) ==
             CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(8, cx_string_length(str));

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_string_replace(str, 0, 0, cx_string_view_from_cstr("0123456789abcdef")) ==
             CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(8, cx_string_length(str));

    failing.fail_at = 0;
    cx_string_destroy(&str);

    return cx_test_finish("string.failures");
}

int main(void) {
    int failures = 0;

    failures += test_lifecycle();
    failures += test_from_and_copy();
    failures += test_mutation();
    failures += test_insert_erase_replace();
    failures += test_aliasing();
    failures += test_search_and_compare();
    failures += test_counted_allocator();
    failures += test_invalid_arguments();
    failures += test_empty_operations();
    failures += test_overflow();
    failures += test_lifecycle_edges();
    failures += test_allocation_failures();

    return failures == 0 ? 0 : 1;
}
