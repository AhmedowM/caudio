#include <cstdx/allocator_counted.h>
#include <cstdx/sort.h>
#include <cstdx/vector.h>

#include "cx_test.h"

static int compare_int(const void *left, const void *right) {
    int a = *(const int *)left;
    int b = *(const int *)right;

    return (a > b) - (a < b);
}

static int test_int_array(void) {
    int values[] = {5, 3, 8, 1, 9, 2, 7, 4, 6, 0};

    CX_CHECK(cx_sort_array(values, int, 10, compare_int) == CX_OK);

    for (int i = 0; i < 10; ++i) {
        CX_CHECK_EQ(i, values[i]);
    }

    int sorted[] = {1, 2, 3};

    CX_CHECK(cx_sort(sorted, 3, sizeof(int), alignof(int), compare_int,
                     nullptr) == CX_OK);
    CX_CHECK_EQ(1, sorted[0]);
    CX_CHECK_EQ(3, sorted[2]);

    int reversed[] = {3, 2, 1};

    CX_CHECK(cx_sort(reversed, 3, sizeof(int), alignof(int), compare_int,
                     nullptr) == CX_OK);
    CX_CHECK_EQ(1, reversed[0]);
    CX_CHECK_EQ(3, reversed[2]);

    int single[] = {42};

    CX_CHECK(cx_sort(single, 1, sizeof(int), alignof(int), compare_int,
                     nullptr) == CX_OK);
    CX_CHECK_EQ(42, single[0]);

    CX_CHECK(cx_sort(nullptr, 0, sizeof(int), alignof(int), compare_int,
                     nullptr) == CX_OK);
    CX_CHECK(cx_sort(nullptr, 1, sizeof(int), alignof(int), compare_int,
                     nullptr) == CX_OK);

    return cx_test_finish("sort.int_array");
}

typedef struct record {
    int key;
    int order;
} record;

static int compare_record(const void *left, const void *right) {
    auto a = (const record *)left;
    auto b = (const record *)right;

    return (a->key > b->key) - (a->key < b->key);
}

static int test_stability(void) {
    record values[] = {
        {2, 0}, {1, 1}, {2, 2}, {1, 3}, {2, 4}, {1, 5},
    };
    int expected_orders[] = {1, 3, 5, 0, 2, 4};

    CX_CHECK(cx_sort_array(values, record, 6, compare_record) == CX_OK);

    for (int i = 0; i < 3; ++i) {
        CX_CHECK_EQ(1, values[i].key);
        CX_CHECK_EQ(expected_orders[i], values[i].order);
    }

    for (int i = 3; i < 6; ++i) {
        CX_CHECK_EQ(2, values[i].key);
        CX_CHECK_EQ(expected_orders[i], values[i].order);
    }

    return cx_test_finish("sort.stability");
}

static int test_vector(void) {
    cx_allocator_counted counted;

    CX_CHECK(cx_allocator_counted_init(&counted, nullptr) == CX_OK);

    auto allocator = cx_allocator_counted_as_allocator(&counted);
    cx_vector *vector = nullptr;

    CX_CHECK(cx_vector_init_of(&vector, int, nullptr, &allocator) == CX_OK);

    int values[] = {5, 1, 4, 2, 3};

    for (size_t i = 0; i < 5; ++i) {
        CX_CHECK(cx_vector_push_value(vector, values[i]) == CX_OK);
    }

    CX_CHECK(cx_sort_vector(vector, compare_int, &allocator) == CX_OK);

    for (int i = 0; i < 5; ++i) {
        CX_CHECK_EQ(i + 1, cx_vector_at(vector, int, i));
    }

    CX_CHECK(cx_sort_vector(nullptr, compare_int, nullptr) ==
             CX_INVALID_ARGUMENT);

    cx_vector_destroy(&vector);
    CX_CHECK(vector == nullptr);
    CX_CHECK(!cx_allocator_counted_has_leaks(&counted));
    cx_allocator_counted_destroy(&counted);

    return cx_test_finish("sort.vector");
}

static int test_errors(void) {
    int values[] = {2, 1};

    CX_CHECK(cx_sort(values, 2, sizeof(int), alignof(int), nullptr, nullptr) ==
             CX_INVALID_ARGUMENT);
    CX_CHECK(cx_sort(values, 2, 0, alignof(int), compare_int, nullptr) ==
             CX_INVALID_ARGUMENT);
    CX_CHECK(cx_sort(values, 2, sizeof(int), 3, compare_int, nullptr) ==
             CX_INVALID_ARGUMENT);
    CX_CHECK(cx_sort(nullptr, 2, sizeof(int), alignof(int), compare_int,
                     nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_sort((int[]){1}, 1, 0, alignof(int), compare_int, nullptr) ==
             CX_INVALID_ARGUMENT);
    CX_CHECK(cx_sort(nullptr, 0, sizeof(int), alignof(int), compare_int,
                     nullptr) == CX_OK);

    return cx_test_finish("sort.errors");
}

int main(void) {
    int failures = 0;

    failures += test_int_array();
    failures += test_stability();
    failures += test_vector();
    failures += test_errors();

    return failures == 0 ? 0 : 1;
}
