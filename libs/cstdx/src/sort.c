#include <cstdx/sort.h>

#include <stdint.h>
#include <string.h>

#include <cstdx/checked.h>

#include "vector_internal.h"

static uint8_t *element_at(uint8_t *base, size_t element_size, size_t index) {
    return base + index * element_size;
}

static void merge_pass(uint8_t *base, uint8_t *scratch, size_t length,
                       size_t element_size, size_t width,
                       cx_sort_compare_fn compare) {
    size_t step = width <= length / 2 ? width * 2 : length;

    for (size_t start = 0; start < length; start += step) {
        auto middle = width > length - start ? length : start + width;
        auto end = step > length - start ? length : start + step;

        if (middle >= end) {
            memcpy(element_at(scratch, element_size, start),
                   element_at(base, element_size, start),
                   (end - start) * element_size);
            continue;
        }

        size_t left = start;
        size_t right = middle;
        size_t out = start;

        while (left < middle && right < end) {
            auto left_element = element_at(base, element_size, left);
            auto right_element = element_at(base, element_size, right);

            if (compare(right_element, left_element) < 0) {
                memcpy(element_at(scratch, element_size, out), right_element,
                       element_size);
                ++right;
            } else {
                memcpy(element_at(scratch, element_size, out), left_element,
                       element_size);
                ++left;
            }

            ++out;
        }

        while (left < middle) {
            memcpy(element_at(scratch, element_size, out),
                   element_at(base, element_size, left), element_size);
            ++left;
            ++out;
        }

        while (right < end) {
            memcpy(element_at(scratch, element_size, out),
                   element_at(base, element_size, right), element_size);
            ++right;
            ++out;
        }
    }
}

cx_status cx_sort(void *base, size_t length, size_t element_size,
                  size_t element_align, cx_sort_compare_fn compare,
                  const cx_allocator *allocator) {
    if (compare == nullptr || element_size == 0 ||
        !cx_is_power_of_two(element_align) ||
        !cx_allocator_is_valid(allocator)) {
        return CX_INVALID_ARGUMENT;
    }

    if (length < 2) {
        return CX_OK;
    }

    if (base == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    size_t bytes;

    if (cx_mul_overflow(length, element_size, &bytes)) {
        return CX_OVERFLOW;
    }

    auto scratch = cx_allocator_alloc(allocator, bytes, element_align);

    if (scratch == nullptr) {
        return CX_OUT_OF_MEMORY;
    }

    auto source = (uint8_t *)base;

    for (size_t width = 1; width < length;) {
        merge_pass(source, scratch, length, element_size, width, compare);
        memcpy(source, scratch, bytes);

        if (width > length / 2) {
            break;
        }

        width *= 2;
    }

    cx_allocator_free(allocator, scratch, bytes, element_align);
    return CX_OK;
}

cx_status cx_sort_vector(cx_vector *vector, cx_sort_compare_fn compare,
                         const cx_allocator *allocator) {
    if (vector == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    return cx_sort(vector->data, vector->length, vector->element_size,
                   vector->element_align, compare, allocator);
}
