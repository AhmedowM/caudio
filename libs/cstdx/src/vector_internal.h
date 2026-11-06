#ifndef CSTDX_VECTOR_INTERNAL_H
#define CSTDX_VECTOR_INTERNAL_H

/**
 * @file vector_internal.h
 * @brief Private definition of the opaque cx_vector handle.
 */

#include <cstdx/vector.h>

struct cx_vector {
    void *data;
    size_t length;
    size_t capacity;
    size_t element_size;
    size_t element_align;
    const cx_vector_ops *ops;
    const cx_allocator *allocator;
};

/**
 * @brief Initialize caller-owned vector storage without allocating a handle.
 */
cx_status cx_impl_vector_init(cx_vector *vector, size_t element_size,
                              size_t element_align, const cx_vector_ops *ops,
                              const cx_allocator *allocator);

/**
 * @brief Destroy elements, release storage, and reset caller-owned storage.
 *
 * Does not free @p vector itself. The allocator is preserved so the storage
 * can be reused.
 */
void cx_impl_vector_destroy(cx_vector *vector);

#endif /* CSTDX_VECTOR_INTERNAL_H */
