#ifndef CSTDX_SORT_H
#define CSTDX_SORT_H

/**
 * @file sort.h
 * @brief Stable generic sorting.
 *
 * `cx_sort` sorts an array of fixed-size elements in place. It is a stable
 * merge sort, so elements that compare equal keep their relative order.
 */

#include <cstdx/allocator.h>
#include <cstdx/api.h>
#include <cstdx/status.h>
#include <cstdx/vector.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Three-way comparison of two elements.
 *
 * Return a negative value when @p left sorts before @p right, zero when they
 * are equivalent, and a positive value when @p left sorts after @p right.
 */
typedef int (*cx_sort_compare_fn)(const void* left, const void* right);

/**
 * @brief Stably sort @p length elements in place.
 *
 * @param base First element; may be null when @p length is less than two.
 * @param length Number of elements.
 * @param element_size Size of one element in bytes; must be non-zero.
 * @param element_align Alignment of the element type; must be a power of two.
 * @param compare Comparison callback; must not be null.
 * @param allocator Allocator for the merge scratch buffer, or nullptr for the
 *                  default allocator.
 *
 * A temporary buffer of `length * element_size` bytes is allocated. On
 * allocation failure the array is left unchanged and `CX_OUT_OF_MEMORY` is
 * returned.
 */
[[nodiscard]] CX_API cx_status cx_sort(void* base, size_t length, size_t element_size,
                                       size_t element_align, cx_sort_compare_fn compare,
                                       const cx_allocator* allocator);

/**
 * @brief Stably sort the live elements of a vector.
 *
 * Uses the vector's element size and alignment. The vector's storage is
 * sorted in place; ownership of elements is unchanged because elements are
 * moved as raw bytes.
 */
[[nodiscard]] CX_API cx_status cx_sort_vector(cx_vector* vector, cx_sort_compare_fn compare,
                                              const cx_allocator* allocator);

/**
 * @brief Sort a typed array.
 *
 * @param array Pointer to the first element.
 * @param T Element type.
 * @param length Number of elements.
 * @param compare Comparison callback.
 */
#define cx_sort_array(array, T, length, compare)                                                   \
    cx_sort((array), (length), sizeof(T), alignof(T), (compare), nullptr)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_SORT_H */
