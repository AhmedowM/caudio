#ifndef CSTDX_BYTES_INTERNAL_H
#define CSTDX_BYTES_INTERNAL_H

/**
 * @file bytes_internal.h
 * @brief Private definition of the opaque cx_bytes handle.
 */

#include <cstdx/bytes.h>

struct cx_bytes {
    uint8_t* data;
    size_t length;
    size_t capacity;
    const cx_allocator* allocator;
};

/**
 * @brief Initialize caller-owned buffer storage without allocating a handle.
 */
cx_status cx_impl_bytes_init(cx_bytes* bytes, const cx_allocator* allocator);

/**
 * @brief Release the buffer and reset the caller-owned storage.
 *
 * Does not free @p bytes itself. The allocator is preserved so the storage can
 * be reused.
 */
void cx_impl_bytes_destroy(cx_bytes* bytes);

#endif /* CSTDX_BYTES_INTERNAL_H */
