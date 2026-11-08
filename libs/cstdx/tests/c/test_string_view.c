#include <cstdx/string_view.h>
#include <string.h>

#include "cx_test.h"

static bool matches(cx_string_view view, const char* cstr) {
    auto length = strlen(cstr);

    return view.length == length && (length == 0 || memcmp(view.data, cstr, length) == 0);
}

static int test_construction(void) {
    auto view = cx_string_view_from_cstr("hello");

    CX_CHECK(matches(view, "hello"));
    CX_CHECK_EQ(5, view.length);

    auto empty = cx_string_view_from_cstr(nullptr);

    CX_CHECK_EQ(0, empty.length);
    CX_CHECK(empty.data == nullptr);

    auto forced = cx_string_view_from_parts(nullptr, 5);

    CX_CHECK_EQ(0, forced.length);
    CX_CHECK(forced.data == nullptr);

    auto parts = cx_string_view_from_parts("abcdef", 3);

    CX_CHECK(matches(parts, "abc"));

    return cx_test_finish("string_view.construction");
}

static int test_sub(void) {
    auto view = cx_string_view_from_cstr("hello");
    cx_string_view sub = {};

    CX_CHECK(cx_string_view_sub(view, 1, 3, &sub) == CX_OK);
    CX_CHECK(matches(sub, "ell"));

    CX_CHECK(cx_string_view_sub(view, 5, 0, &sub) == CX_OK);
    CX_CHECK_EQ(0, sub.length);

    CX_CHECK(cx_string_view_sub(view, 6, 0, &sub) == CX_OUT_OF_RANGE);
    CX_CHECK(cx_string_view_sub(view, 4, 2, &sub) == CX_OUT_OF_RANGE);
    CX_CHECK(cx_string_view_sub(view, 0, 0, nullptr) == CX_INVALID_ARGUMENT);

    return cx_test_finish("string_view.sub");
}

static int test_compare_and_find(void) {
    auto hello = cx_string_view_from_cstr("hello");
    auto world = cx_string_view_from_cstr("world");
    auto hell = cx_string_view_from_cstr("hell");

    CX_CHECK(cx_string_view_eq(hello, hello));
    CX_CHECK(!cx_string_view_eq(hello, world));
    CX_CHECK(
        cx_string_view_eq(cx_string_view_from_cstr(""), cx_string_view_from_parts(nullptr, 0)));
    CX_CHECK_EQ(CX_STRING_LESS, cx_string_view_compare(hell, hello));
    CX_CHECK_EQ(CX_STRING_GREATER, cx_string_view_compare(world, hello));
    CX_CHECK_EQ(CX_STRING_EQUAL, cx_string_view_compare(hello, hello));
    CX_CHECK(cx_string_view_starts_with(hello, cx_string_view_from_cstr("he")));
    CX_CHECK(!cx_string_view_starts_with(hello, world));
    CX_CHECK(cx_string_view_ends_with(hello, cx_string_view_from_cstr("lo")));
    CX_CHECK(!cx_string_view_ends_with(hello, cx_string_view_from_cstr("he")));

    auto haystack = cx_string_view_from_cstr("hello world");
    size_t index = 0;

    CX_CHECK(cx_string_view_find(haystack, cx_string_view_from_cstr("world"), &index) == CX_OK);
    CX_CHECK_EQ(6, index);
    CX_CHECK(cx_string_view_find(haystack, cx_string_view_from_cstr(""), &index) == CX_OK);
    CX_CHECK_EQ(0, index);
    CX_CHECK(cx_string_view_find(haystack, cx_string_view_from_cstr("xyz"), &index) ==
             CX_NOT_FOUND);
    CX_CHECK(cx_string_view_find(haystack, cx_string_view_from_cstr("world"), nullptr) ==
             CX_INVALID_ARGUMENT);

    return cx_test_finish("string_view.compare_and_find");
}

static int test_split(void) {
    cx_string_view_split split;
    cx_string_view piece;
    auto input = cx_string_view_from_cstr("a,b,,c,");

    cx_string_view_split_init(&split, input, cx_string_view_from_cstr(","));

    CX_CHECK(cx_string_view_split_next(&split, &piece));
    CX_CHECK(matches(piece, "a"));
    CX_CHECK(cx_string_view_split_next(&split, &piece));
    CX_CHECK(matches(piece, "b"));
    CX_CHECK(cx_string_view_split_next(&split, &piece));
    CX_CHECK(matches(piece, ""));
    CX_CHECK(cx_string_view_split_next(&split, &piece));
    CX_CHECK(matches(piece, "c"));
    CX_CHECK(cx_string_view_split_next(&split, &piece));
    CX_CHECK(matches(piece, ""));
    CX_CHECK(!cx_string_view_split_next(&split, &piece));

    cx_string_view_split_init(&split, cx_string_view_from_cstr(""), cx_string_view_from_cstr(","));
    CX_CHECK(cx_string_view_split_next(&split, &piece));
    CX_CHECK(matches(piece, ""));
    CX_CHECK(!cx_string_view_split_next(&split, &piece));

    cx_string_view_split_init(&split, input, cx_string_view_from_cstr(""));
    CX_CHECK(cx_string_view_split_next(&split, &piece));
    CX_CHECK(matches(piece, "a,b,,c,"));
    CX_CHECK(!cx_string_view_split_next(&split, &piece));

    CX_CHECK(!cx_string_view_split_next(nullptr, &piece));
    CX_CHECK(!cx_string_view_split_next(&split, nullptr));

    return cx_test_finish("string_view.split");
}

static int test_edges(void) {
    auto hello = cx_string_view_from_cstr("hello");
    auto hell = cx_string_view_from_cstr("hell");
    auto empty = cx_string_view_from_cstr("");
    cx_string_view null_nonempty = {nullptr, 3};
    cx_string_view null_one = {nullptr, 1};

    /* Equality: mismatched lengths, and equal lengths with no data. */
    CX_CHECK(!cx_string_view_eq(hello, hell));
    CX_CHECK(!cx_string_view_eq(null_nonempty, null_nonempty));

    /* A longer view sharing a prefix with a shorter one is greater. */
    CX_CHECK_EQ(CX_STRING_GREATER, cx_string_view_compare(hello, hell));

    /* starts_with: longer prefix, empty prefix, and missing data. */
    CX_CHECK(!cx_string_view_starts_with(hello, cx_string_view_from_cstr("hellos")));
    CX_CHECK(cx_string_view_starts_with(hello, empty));
    CX_CHECK(!cx_string_view_starts_with(hello, null_one));

    /* ends_with: longer suffix, empty suffix, and missing data. */
    CX_CHECK(!cx_string_view_ends_with(hello, cx_string_view_from_cstr("ohello")));
    CX_CHECK(cx_string_view_ends_with(hello, empty));
    CX_CHECK(!cx_string_view_ends_with(hello, null_one));

    /* A null split cursor is accepted and ignored. */
    cx_string_view_split_init(nullptr, hello, empty);

    return cx_test_finish("string_view.edges");
}

int main(void) {
    int failures = 0;

    failures += test_construction();
    failures += test_sub();
    failures += test_compare_and_find();
    failures += test_split();
    failures += test_edges();

    return failures == 0 ? 0 : 1;
}
