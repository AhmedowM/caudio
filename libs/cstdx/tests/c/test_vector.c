#include <cstdx/allocator_counted.h>
#include <cstdx/vector.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cx_test.h"
#include "cx_test_allocators.h"

static int test_pod(void) {
    cx_vector *vector = nullptr;

    CX_CHECK(cx_vector_init_of(&vector, int, nullptr, nullptr) == CX_OK);
    CX_CHECK(vector != nullptr);
    CX_CHECK(cx_vector_empty(vector));
    CX_CHECK_EQ(0, cx_vector_length(vector));
    CX_CHECK_EQ(0, cx_vector_capacity(vector));
    CX_CHECK_EQ(sizeof(int), cx_vector_element_size(vector));
    CX_CHECK(cx_vector_ptr(vector, 0) == nullptr);
    CX_CHECK(cx_vector_ptr_mut(vector, 0) == nullptr);
    CX_CHECK(cx_vector_as_slice(vector).length == 0);

    for (int i = 0; i < 10; ++i) {
        CX_CHECK(cx_vector_push_value(vector, i) == CX_OK);
    }

    CX_CHECK_EQ(10, cx_vector_length(vector));
    CX_CHECK(cx_vector_capacity(vector) >= 10);

    for (int i = 0; i < 10; ++i) {
        CX_CHECK_EQ(i, cx_vector_at(vector, int, i));
    }

    int value = 42;

    CX_CHECK(cx_vector_push(vector, &value) == CX_OK);
    CX_CHECK_EQ(42, cx_vector_at(vector, int, 10));
    CX_CHECK_EQ(11, cx_vector_length(vector));

    CX_CHECK(cx_vector_insert_copy(vector, 5, &value, sizeof(value)) == CX_OK);
    CX_CHECK_EQ(12, cx_vector_length(vector));
    CX_CHECK_EQ(42, cx_vector_at(vector, int, 5));
    CX_CHECK_EQ(4, cx_vector_at(vector, int, 4));
    CX_CHECK_EQ(5, cx_vector_at(vector, int, 6));

    CX_CHECK(cx_vector_remove(vector, 5) == CX_OK);
    CX_CHECK_EQ(11, cx_vector_length(vector));
    CX_CHECK_EQ(5, cx_vector_at(vector, int, 5));

    int last = 0;

    CX_CHECK(cx_vector_pop(vector, &last) == CX_OK);
    CX_CHECK_EQ(42, last);
    CX_CHECK_EQ(10, cx_vector_length(vector));

    CX_CHECK(cx_vector_swap_remove(vector, 0) == CX_OK);
    CX_CHECK_EQ(9, cx_vector_length(vector));
    CX_CHECK_EQ(9, cx_vector_at(vector, int, 0));

    auto slice = cx_vector_as_slice(vector);

    CX_CHECK_EQ((size_t)9 * sizeof(int), slice.length);
    CX_CHECK(slice.data == cx_vector_data_ptr(vector));
    CX_CHECK(cx_vector_data(vector, int) == (int *)cx_vector_data_ptr(vector));

    cx_vector_clear(vector);
    CX_CHECK(cx_vector_empty(vector));
    CX_CHECK(cx_vector_capacity(vector) > 0);

    CX_CHECK(cx_vector_shrink_to_fit(vector) == CX_OK);
    CX_CHECK_EQ(0, cx_vector_capacity(vector));
    CX_CHECK(cx_vector_data_ptr(vector) == nullptr);

    CX_CHECK(cx_vector_push_value(vector, value) == CX_OK);
    CX_CHECK(cx_vector_shrink_to_fit(vector) == CX_OK);
    CX_CHECK_EQ(1, cx_vector_capacity(vector));
    CX_CHECK_EQ(42, cx_vector_at(vector, int, 0));

    /* Shrinking an already-tight vector is a no-op. */
    CX_CHECK(cx_vector_shrink_to_fit(vector) == CX_OK);
    CX_CHECK_EQ(1, cx_vector_capacity(vector));

    cx_vector_destroy(&vector);
    CX_CHECK(vector == nullptr);
    cx_vector_destroy(&vector);

    return cx_test_finish("vector.pod");
}

static int test_errors(void) {
    cx_vector *vector = nullptr;

    CX_CHECK(cx_vector_init(nullptr, 4, 4, nullptr, nullptr) ==
             CX_INVALID_ARGUMENT);
    CX_CHECK(cx_vector_init(&vector, 0, 4, nullptr, nullptr) ==
             CX_INVALID_ARGUMENT);
    CX_CHECK(cx_vector_init(&vector, 4, 3, nullptr, nullptr) ==
             CX_INVALID_ARGUMENT);
    CX_CHECK(cx_vector_init_of(&vector, int, nullptr, nullptr) == CX_OK);

    int value = 7;

    CX_CHECK(cx_vector_push_copy(vector, &value, sizeof(value) + 1) ==
             CX_INVALID_ARGUMENT);
    CX_CHECK(cx_vector_push_copy(vector, nullptr, sizeof(value)) ==
             CX_INVALID_ARGUMENT);
    CX_CHECK(cx_vector_insert_copy(vector, 1, &value, sizeof(value)) ==
             CX_OUT_OF_RANGE);
    CX_CHECK(cx_vector_insert_copy(vector, 0, &value, sizeof(value)) == CX_OK);
    CX_CHECK(cx_vector_insert_copy(vector, 1, &value, sizeof(value)) == CX_OK);
    CX_CHECK_EQ(2, cx_vector_length(vector));

    CX_CHECK(cx_vector_ptr(vector, 2) == nullptr);
    CX_CHECK(cx_vector_ptr_mut(vector, 2) == nullptr);
    CX_CHECK(cx_vector_remove(vector, 2) == CX_OUT_OF_RANGE);
    CX_CHECK(cx_vector_swap_remove(vector, 2) == CX_OUT_OF_RANGE);
    CX_CHECK(cx_vector_pop(vector, &value) == CX_OK);
    CX_CHECK(cx_vector_pop(vector, nullptr) == CX_OK);
    CX_CHECK(cx_vector_pop(vector, nullptr) == CX_OUT_OF_RANGE);

    cx_vector_destroy(&vector);

    cx_vector_ops bad = {};

    CX_CHECK(cx_vector_init_of(&vector, int, &bad, nullptr) ==
             CX_INVALID_ARGUMENT);

    return cx_test_finish("vector.errors");
}

static void vector_noop_copy(void *dst, const void *src) {
    memcpy(dst, src, sizeof(int));
}

static int test_invalid_arguments(void) {
    cx_vector *vector = nullptr;
    cx_allocator invalid = {0};
    int value = 7;
    size_t size = sizeof(int);

    CX_CHECK(cx_vector_init(&vector, sizeof(int), alignof(int), nullptr,
                            &invalid) == CX_INVALID_ARGUMENT);

    cx_vector_ops missing_destroy = {
        .copy = vector_noop_copy,
        .destroy = nullptr,
    };

    CX_CHECK(cx_vector_init_of(&vector, int, &missing_destroy, nullptr) ==
             CX_INVALID_ARGUMENT);

    CX_CHECK(cx_vector_reserve(nullptr, 4) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_vector_shrink_to_fit(nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_vector_push_copy(nullptr, &value, size) ==
             CX_INVALID_ARGUMENT);
    CX_CHECK(cx_vector_insert_copy(nullptr, 0, &value, size) ==
             CX_INVALID_ARGUMENT);
    CX_CHECK(cx_vector_pop(nullptr, nullptr) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_vector_remove(nullptr, 0) == CX_INVALID_ARGUMENT);
    CX_CHECK(cx_vector_swap_remove(nullptr, 0) == CX_INVALID_ARGUMENT);

    cx_vector_clear(nullptr);

    CX_CHECK(cx_vector_ptr(nullptr, 0) == nullptr);
    CX_CHECK(cx_vector_ptr_mut(nullptr, 0) == nullptr);
    CX_CHECK(cx_vector_ptr_typed(nullptr, 0, size) == nullptr);
    CX_CHECK(cx_vector_ptr_mut_typed(nullptr, 0, size) == nullptr);
    CX_CHECK_EQ(0, cx_vector_element_align(nullptr));
    CX_CHECK(cx_vector_data_ptr(nullptr) == nullptr);
    CX_CHECK(cx_vector_data_ptr_const(nullptr) == nullptr);
    CX_CHECK_EQ(0, cx_vector_length(nullptr));
    CX_CHECK_EQ(0, cx_vector_capacity(nullptr));
    CX_CHECK_EQ(0, cx_vector_element_size(nullptr));
    CX_CHECK(cx_vector_empty(nullptr));
    CX_CHECK_EQ(0, cx_vector_as_slice(nullptr).length);

    CX_CHECK(cx_vector_init_of(&vector, int, nullptr, nullptr) == CX_OK);
    CX_CHECK(cx_vector_push_value(vector, value) == CX_OK);
    CX_CHECK(cx_vector_ptr_typed(vector, 0, size + 1) == nullptr);
    CX_CHECK(cx_vector_ptr_typed(vector, 1, size) == nullptr);
    CX_CHECK(cx_vector_ptr_mut_typed(vector, 0, size + 1) == nullptr);
    CX_CHECK(cx_vector_ptr_mut_typed(vector, 1, size) == nullptr);
    CX_CHECK(cx_vector_ptr_typed(vector, 0, size) != nullptr);
    CX_CHECK(cx_vector_ptr_mut_typed(vector, 0, size) != nullptr);
    CX_CHECK(cx_vector_ptr(vector, 0) == cx_vector_ptr_mut(vector, 0));

    /* Popping into the vector's own storage is rejected. */
    CX_CHECK(cx_vector_pop(vector, cx_vector_ptr_mut(vector, 0)) ==
             CX_INVALID_ARGUMENT);
    CX_CHECK_EQ(1, cx_vector_length(vector));

    cx_vector_destroy(&vector);

    return cx_test_finish("vector.invalid_arguments");
}

static int test_overflow(void) {
    cx_vector *vector = nullptr;

    CX_CHECK(cx_vector_init_of(&vector, int, nullptr, nullptr) == CX_OK);
    CX_CHECK(cx_vector_reserve(vector, SIZE_MAX) == CX_OVERFLOW);
    CX_CHECK_EQ(0, cx_vector_capacity(vector));

    cx_vector_destroy(&vector);

    return cx_test_finish("vector.overflow");
}

static int test_allocation_failures(void) {
    cx_allocator_failing failing = {
        .fail_at = 1,
        .fail_all_after = false,
    };
    cx_allocator allocator = cx_allocator_failing_as_allocator(&failing);
    cx_vector *vector = nullptr;
    int value = 7;

    CX_CHECK(cx_vector_init_of(&vector, int, nullptr, &allocator) ==
             CX_OUT_OF_MEMORY);
    CX_CHECK(vector == nullptr);

    failing.attempts = 0;
    failing.fail_at = 0;
    CX_CHECK(cx_vector_init_of(&vector, int, nullptr, &allocator) == CX_OK);

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_vector_push_copy(vector, &value, sizeof(value)) ==
             CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(0, cx_vector_length(vector));
    CX_CHECK_EQ(0, cx_vector_capacity(vector));

    failing.fail_at = 0;
    CX_CHECK(cx_vector_push_copy(vector, &value, sizeof(value)) == CX_OK);
    CX_CHECK_EQ(1, cx_vector_length(vector));
    CX_CHECK(cx_vector_capacity(vector) >= 8);

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_vector_reserve(vector, 64) == CX_OUT_OF_MEMORY);

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_vector_shrink_to_fit(vector) == CX_OUT_OF_MEMORY);
    CX_CHECK(cx_vector_capacity(vector) >= 8);

    /* Pushing or inserting an aliased element stages a detached copy. */
    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_vector_push_copy(vector, cx_vector_data_ptr(vector),
                                 sizeof(value)) == CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(1, cx_vector_length(vector));

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_vector_insert_copy(vector, 0, cx_vector_data_ptr(vector),
                                   sizeof(value)) == CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(1, cx_vector_length(vector));

    /* Fill the initial capacity, then force a grow on insert and push. */
    failing.fail_at = 0;

    for (int i = 0; i < 7; ++i) {
        CX_CHECK(cx_vector_push_value(vector, i) == CX_OK);
    }

    CX_CHECK_EQ(8, cx_vector_length(vector));

    int remote = 9;

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_vector_insert_copy(vector, 0, &remote, sizeof(remote)) ==
             CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(8, cx_vector_length(vector));

    failing.fail_at = failing.attempts + 1;
    CX_CHECK(cx_vector_push_copy(vector, &remote, sizeof(remote)) ==
             CX_OUT_OF_MEMORY);
    CX_CHECK_EQ(8, cx_vector_length(vector));

    failing.fail_at = 0;
    cx_vector_destroy(&vector);

    return cx_test_finish("vector.failures");
}

/* ------------------------------------------------------------------------- */
/* Owning element type used to exercise copy/move/destroy callbacks.          */
/* ------------------------------------------------------------------------- */

typedef struct owned {
    int *value;
} owned;

static int owned_live = 0;
static int owned_copies = 0;
static int owned_moves = 0;
static int owned_destroys = 0;

static owned owned_make(int value) {
    auto item = (owned){
        .value = malloc(sizeof(int)),
    };

    if (item.value != nullptr) {
        *item.value = value;
    }

    return item;
}

static void owned_release(owned *item) {
    if (item->value != nullptr) {
        free(item->value);
        item->value = nullptr;
    }
}

static void owned_copy(void *dst, const void *src) {
    auto destination = (owned *)dst;
    auto source = (const owned *)src;

    destination->value = malloc(sizeof(int));

    if (destination->value != nullptr) {
        *destination->value = *source->value;
    }

    owned_live++;
    owned_copies++;
}

static void owned_move(void *dst, void *src) {
    auto destination = (owned *)dst;
    auto source = (owned *)src;

    destination->value = source->value;
    source->value = nullptr;
    owned_moves++;
}

static void owned_destroy(void *element) {
    auto item = (owned *)element;

    if (item->value != nullptr) {
        free(item->value);
        item->value = nullptr;
        owned_live--;
    }

    owned_destroys++;
}

static const cx_vector_ops owned_ops = {
    .copy = owned_copy,
    .move = owned_move,
    .destroy = owned_destroy,
};

static int test_owning(void) {
    cx_allocator_counted counted;

    CX_CHECK(cx_allocator_counted_init(&counted, nullptr) == CX_OK);

    auto allocator = cx_allocator_counted_as_allocator(&counted);
    cx_vector *vector = nullptr;

    CX_CHECK(cx_vector_init_of(&vector, owned, &owned_ops, &allocator) ==
             CX_OK);
    CX_CHECK_EQ(sizeof(owned), cx_vector_element_size(vector));

    for (int i = 0; i < 5; ++i) {
        auto item = owned_make(i);

        CX_CHECK(cx_vector_push(vector, &item) == CX_OK);
        owned_release(&item);
    }

    CX_CHECK_EQ(5, cx_vector_length(vector));
    CX_CHECK_EQ(5, owned_live);

    for (int i = 0; i < 5; ++i) {
        CX_CHECK_EQ(i, *cx_vector_at(vector, owned, i).value);
    }

    auto extra = owned_make(99);

    CX_CHECK(cx_vector_insert_copy(vector, 2, &extra, sizeof(extra)) == CX_OK);
    owned_release(&extra);
    CX_CHECK_EQ(6, cx_vector_length(vector));
    CX_CHECK_EQ(6, owned_live);
    CX_CHECK_EQ(99, *cx_vector_at(vector, owned, 2).value);

    CX_CHECK(cx_vector_remove(vector, 2) == CX_OK);
    CX_CHECK_EQ(5, cx_vector_length(vector));
    CX_CHECK_EQ(5, owned_live);

    CX_CHECK(cx_vector_swap_remove(vector, 0) == CX_OK);
    CX_CHECK_EQ(4, cx_vector_length(vector));
    CX_CHECK_EQ(4, owned_live);

    owned popped = {};

    CX_CHECK(cx_vector_pop(vector, &popped) == CX_OK);
    CX_CHECK(popped.value != nullptr);
    CX_CHECK_EQ(3, cx_vector_length(vector));
    CX_CHECK_EQ(4, owned_live);
    owned_destroy(&popped);
    CX_CHECK_EQ(3, owned_live);

    owned *first = cx_vector_ptr_mut(vector, 0);

    CX_CHECK(first != nullptr);
    CX_CHECK(cx_vector_push(vector, first) == CX_OK);
    CX_CHECK_EQ(4, cx_vector_length(vector));
    CX_CHECK_EQ(4, owned_live);

    CX_CHECK(cx_vector_shrink_to_fit(vector) == CX_OK);
    CX_CHECK_EQ(4, cx_vector_capacity(vector));
    CX_CHECK_EQ(4, owned_live);

    cx_vector_clear(vector);
    CX_CHECK(cx_vector_empty(vector));
    CX_CHECK_EQ(0, owned_live);

    for (int i = 0; i < 3; ++i) {
        auto item = owned_make(i);

        CX_CHECK(cx_vector_push(vector, &item) == CX_OK);
        owned_release(&item);
    }

    CX_CHECK_EQ(3, owned_live);
    cx_vector_destroy(&vector);
    CX_CHECK_EQ(0, owned_live);
    CX_CHECK(owned_copies > 0);
    CX_CHECK(owned_moves > 0);
    CX_CHECK(owned_destroys > 0);
    CX_CHECK(!cx_allocator_counted_has_leaks(&counted));

    cx_allocator_counted_destroy(&counted);

    return cx_test_finish("vector.owning");
}

static const cx_vector_ops owned_ops_no_move = {
    .copy = owned_copy,
    .destroy = owned_destroy,
};

static int test_ops_without_move(void) {
    cx_vector *vector = nullptr;

    CX_CHECK(cx_vector_init_of(&vector, owned, &owned_ops_no_move, nullptr) ==
             CX_OK);

    for (int i = 0; i < 3; ++i) {
        auto item = owned_make(i);

        CX_CHECK(cx_vector_push(vector, &item) == CX_OK);
        owned_release(&item);
    }

    CX_CHECK_EQ(3, owned_live);

    auto extra = owned_make(9);

    CX_CHECK(cx_vector_insert_copy(vector, 1, &extra, sizeof(extra)) == CX_OK);
    owned_release(&extra);
    CX_CHECK_EQ(4, owned_live);

    /* Removing the last element takes the single-destroy path. */
    CX_CHECK(cx_vector_swap_remove(vector, 3) == CX_OK);
    CX_CHECK_EQ(3, owned_live);

    CX_CHECK(cx_vector_remove(vector, 0) == CX_OK);
    CX_CHECK_EQ(2, owned_live);

    cx_vector_destroy(&vector);
    CX_CHECK_EQ(0, owned_live);

    return cx_test_finish("vector.ops_without_move");
}

static int test_counted_allocator(void) {
    cx_allocator_counted counted;

    CX_CHECK(cx_allocator_counted_init(&counted, nullptr) == CX_OK);

    auto allocator = cx_allocator_counted_as_allocator(&counted);
    cx_vector *vector = nullptr;

    CX_CHECK(cx_vector_init_of(&vector, int, nullptr, &allocator) == CX_OK);

    for (int i = 0; i < 100; ++i) {
        CX_CHECK(cx_vector_push_value(vector, i) == CX_OK);
    }

    CX_CHECK(cx_vector_reserve(vector, 512) == CX_OK);
    CX_CHECK(cx_vector_capacity(vector) >= 512);
    CX_CHECK_EQ(100, cx_vector_length(vector));

    CX_CHECK(cx_vector_shrink_to_fit(vector) == CX_OK);
    CX_CHECK_EQ(100, cx_vector_capacity(vector));

    for (int i = 0; i < 50; ++i) {
        CX_CHECK(cx_vector_remove(vector, 0) == CX_OK);
    }

    CX_CHECK_EQ(50, cx_vector_length(vector));

    cx_vector_destroy(&vector);
    CX_CHECK(!cx_allocator_counted_has_leaks(&counted));

    cx_allocator_counted_destroy(&counted);

    return cx_test_finish("vector.counted_allocator");
}

int main(void) {
    int failures = 0;

    failures += test_pod();
    failures += test_errors();
    failures += test_invalid_arguments();
    failures += test_overflow();
    failures += test_allocation_failures();
    failures += test_owning();
    failures += test_ops_without_move();
    failures += test_counted_allocator();

    return failures == 0 ? 0 : 1;
}
