#include <cstdx/allocator_counted.h>
#include <cstdx/string.h>

#include <string.h>

#include "cx_test.h"

static int test_lifecycle(void) {
    cx_string *str = nullptr;

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
    cx_string *a = nullptr;
    cx_string *b = nullptr;

    CX_CHECK(cx_string_from(&a, "alpha", nullptr) == CX_OK);
    CX_CHECK(cx_string_from_view(&b, cx_string_view_from_cstr("beta"),
                                 nullptr) == CX_OK);
    CX_CHECK(cx_string_equals_cstr(a, "alpha"));
    CX_CHECK(cx_string_equals_cstr(b, "beta"));

    cx_string *copy = nullptr;

    CX_CHECK(cx_string_copy(&copy, a) == CX_OK);
    CX_CHECK(cx_string_equals(copy, a));
    CX_CHECK(cx_string_cstr(copy) != cx_string_cstr(a));

    cx_string *moved = nullptr;

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
    cx_string *str = nullptr;

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
    cx_string *str = nullptr;

    CX_CHECK(cx_string_from(&str, "hello", nullptr) == CX_OK);
    CX_CHECK(cx_string_insert(str, 5, cx_string_view_from_cstr(" world")) ==
             CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello world") == 0);

    CX_CHECK(cx_string_insert(str, 0, cx_string_view_from_cstr(">> ")) ==
             CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), ">> hello world") == 0);

    CX_CHECK(cx_string_erase(str, 0, 3) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello world") == 0);

    CX_CHECK(cx_string_replace(str, 6, 5, cx_string_view_from_cstr("there")) ==
             CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello there") == 0);

    CX_CHECK(cx_string_replace(str, 5, 0, cx_string_view_from_cstr(",")) ==
             CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello, there") == 0);

    CX_CHECK(cx_string_erase(str, 5, 100) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "hello") == 0);
    CX_CHECK(cx_string_insert(str, 6, cx_string_view_from_cstr("x")) ==
             CX_OUT_OF_RANGE);
    CX_CHECK(cx_string_erase(str, 6, 1) == CX_OUT_OF_RANGE);
    CX_CHECK(cx_string_replace(str, 6, 0, cx_string_view_from_cstr("x")) ==
             CX_OUT_OF_RANGE);

    cx_string_destroy(&str);

    return cx_test_finish("string.insert_erase_replace");
}

static int test_aliasing(void) {
    cx_string *str = nullptr;

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

    CX_CHECK(cx_string_view_sub(cx_string_view_of(str), 3, 6, &middle) ==
             CX_OK);
    CX_CHECK(cx_string_replace(str, 0, 3, middle) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(str), "abcabcabcabcabc") == 0);

    cx_string_destroy(&str);

    return cx_test_finish("string.aliasing");
}

static int test_search_and_compare(void) {
    cx_string *str = nullptr;
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

    cx_string *other = nullptr;

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
    cx_string *first = nullptr;
    cx_string *second = nullptr;

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

int main(void) {
    int failures = 0;

    failures += test_lifecycle();
    failures += test_from_and_copy();
    failures += test_mutation();
    failures += test_insert_erase_replace();
    failures += test_aliasing();
    failures += test_search_and_compare();
    failures += test_counted_allocator();

    return failures == 0 ? 0 : 1;
}
