#ifndef CSTDX_STRING_INTERNAL_H
#define CSTDX_STRING_INTERNAL_H

/**
 * @file string_internal.h
 * @brief Private definition of the opaque cx_string handle.
 *
 * The public header only forward-declares cx_string. Every translation unit
 * that needs the layout includes this header instead.
 */

#include <cstdx/string.h>

struct cx_string {
    char* data;
    size_t length;
    size_t capacity;
    const cx_allocator* allocator;
};

/**
 * @brief Initialize caller-owned string storage without allocating a handle.
 *
 * Used by cx_string_builder for its embedded string.
 */
cx_status cx_impl_string_init(cx_string* str, const cx_allocator* allocator);

/**
 * @brief Release the buffer and reset the caller-owned storage.
 *
 * Does not free @p str itself. The allocator is preserved so the storage can
 * be reused.
 */
void cx_impl_string_destroy(cx_string* str);

#endif /* CSTDX_STRING_INTERNAL_H */
