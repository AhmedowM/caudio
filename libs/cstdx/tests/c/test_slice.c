#include <cstdx/slice.h>

#include <string.h>

#include "cx_test.h"

static int test_construction(void) {
    uint8_t data[] = {1, 2, 3, 4, 5};
    auto slice = cx_slice_from_parts(data, sizeof(data));

    CX_CHECK(slice.data == data);
    CX_CHECK_EQ(5, slice.length);
    CX_CHECK(!cx_slice_empty(slice));

    auto empty = cx_slice_from_parts(nullptr, 7);

    CX_CHECK(empty.data == nullptr);
    CX_CHECK_EQ(0, empty.length);
    CX_CHECK(cx_slice_empty(empty));

    auto zero = cx_slice_from_parts(data, 0);

    CX_CHECK(zero.data == data);
    CX_CHECK_EQ(0, zero.length);

    return cx_test_finish("slice.construction");
}

static int test_sub(void) {
    uint8_t data[] = {1, 2, 3, 4, 5};
    auto slice = cx_slice_from_parts(data, sizeof(data));
    cx_slice sub = {};

    CX_CHECK(cx_slice_sub(slice, 1, 3, &sub) == CX_OK);
    CX_CHECK_EQ(3, sub.length);
    CX_CHECK_EQ(2, *(const uint8_t *)sub.data);

    CX_CHECK(cx_slice_sub(slice, 5, 0, &sub) == CX_OK);
    CX_CHECK_EQ(0, sub.length);

    CX_CHECK(cx_slice_sub(slice, 6, 0, &sub) == CX_OUT_OF_RANGE);
    CX_CHECK(cx_slice_sub(slice, 4, 2, &sub) == CX_OUT_OF_RANGE);
    CX_CHECK(cx_slice_sub(slice, 0, 0, nullptr) == CX_INVALID_ARGUMENT);

    return cx_test_finish("slice.sub");
}

static int test_at(void) {
    uint8_t data[] = {10, 20, 30};
    auto slice = cx_slice_from_parts(data, sizeof(data));
    uint8_t value = 0;

    CX_CHECK(cx_slice_at(&slice, 0) == data);
    CX_CHECK_EQ(20, *cx_slice_at(&slice, 1));
    CX_CHECK(cx_slice_at(&slice, 3) == nullptr);
    CX_CHECK(cx_slice_at(nullptr, 0) == nullptr);

    CX_CHECK(cx_slice_get(&slice, 2, &value) == CX_OK);
    CX_CHECK_EQ(30, value);
    CX_CHECK(cx_slice_get(&slice, 3, &value) == CX_OUT_OF_RANGE);
    CX_CHECK(cx_slice_get(nullptr, 0, &value) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_slice_get(&slice, 0, nullptr) == CX_INVALID_ARGUMENT);

    return cx_test_finish("slice.at");
}

static int test_compare_and_search(void) {
    uint8_t left_data[] = {1, 2, 3, 2, 3, 4};
    uint8_t right_data[] = {1, 2, 3, 2, 3, 4};
    uint8_t other_data[] = {1, 2, 3};

    auto left = cx_slice_from_parts(left_data, sizeof(left_data));
    auto right = cx_slice_from_parts(right_data, sizeof(right_data));
    auto other = cx_slice_from_parts(other_data, sizeof(other_data));
    auto empty = cx_slice_from_parts(nullptr, 0);

    CX_CHECK(cx_slice_equals(left, right));
    CX_CHECK(!cx_slice_equals(left, other));
    CX_CHECK(cx_slice_equals(empty, empty));

    CX_CHECK(cx_slice_starts_with(left, other));
    CX_CHECK(!cx_slice_starts_with(other, left));
    CX_CHECK(cx_slice_ends_with(left, cx_slice_from_parts(left_data + 3, 3)));
    CX_CHECK(cx_slice_starts_with(left, empty));
    CX_CHECK(cx_slice_ends_with(left, empty));

    size_t index = 0;

    CX_CHECK(cx_slice_find(left, cx_slice_from_parts(left_data + 1, 2),
                           &index) == CX_OK);
    CX_CHECK_EQ(1, index);
    CX_CHECK(cx_slice_find(left, empty, &index) == CX_OK);
    CX_CHECK_EQ(0, index);
    CX_CHECK(cx_slice_find(left, cx_slice_from_parts(left_data, 7), &index) ==
             CX_NOT_FOUND);
    CX_CHECK(cx_slice_find(left, cx_slice_from_parts(right_data, 1), &index) ==
             CX_OK);
    CX_CHECK_EQ(0, index);
    CX_CHECK(cx_slice_find(left, other, nullptr) == CX_INVALID_ARGUMENT);

    return cx_test_finish("slice.compare_and_search");
}

int main(void) {
    int failures = 0;

    failures += test_construction();
    failures += test_sub();
    failures += test_at();
    failures += test_compare_and_search();

    return failures == 0 ? 0 : 1;
}
