#ifndef CSTDX_SLICE_H
#define CSTDX_SLICE_H

/**
 * @file slice.h
 * @brief Borrowed contiguous byte view (cx_slice).
 *
 * A slice is a pointer plus a length. It does not own memory and is not
 * NUL-terminated. It is the lowest-level borrowed range in cstdx; typed
 * containers expose their contents as a slice of `length * element_size`
 * bytes.
 */

#include <cstdx/api.h>
#include <cstdx/status.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A borrowed range of bytes.
 *
 * `data == nullptr` is only meaningful when `length == 0`.
 */
typedef struct cx_slice {
    const void* data;
    size_t length;
} cx_slice;

/**
 * @brief Build a slice from a pointer and a length.
 *
 * When @p data is nullptr the resulting length is forced to zero.
 */
[[nodiscard]] CX_API cx_slice cx_slice_from_parts(const void* data, size_t length);

/**
 * @brief Extract a sub-slice.
 *
 * @param slice Slice to slice.
 * @param offset First byte to include. Must be at most `slice.length`.
 * @param length Number of bytes to include.
 * @param out Receives the sub-slice.
 * @return `CX_OK`, `CX_INVALID_ARGUMENT` for a null @p out, or
 *         `CX_OUT_OF_RANGE` when the range exceeds @p slice.
 */
[[nodiscard]] CX_API cx_status cx_slice_sub(cx_slice slice, size_t offset, size_t length,
                                            cx_slice* out);

/**
 * @brief Pointer to byte @p index, or nullptr when out of range.
 */
[[nodiscard]] CX_API const uint8_t* cx_slice_at(const cx_slice* slice, size_t index);

/**
 * @brief Read one byte by index.
 *
 * @return `CX_OK`, `CX_INVALID_ARGUMENT`, or `CX_OUT_OF_RANGE`.
 */
[[nodiscard]] CX_API cx_status cx_slice_get(const cx_slice* slice, size_t index, uint8_t* value);

/**
 * @brief Test whether two slices contain the same bytes.
 */
[[nodiscard]] CX_API bool cx_slice_equals(cx_slice left, cx_slice right);

/**
 * @brief Test whether @p slice begins with @p prefix.
 */
[[nodiscard]] CX_API bool cx_slice_starts_with(cx_slice slice, cx_slice prefix);

/**
 * @brief Test whether @p slice ends with @p suffix.
 */
[[nodiscard]] CX_API bool cx_slice_ends_with(cx_slice slice, cx_slice suffix);

/**
 * @brief Find the first occurrence of @p needle in @p haystack.
 *
 * An empty needle is found at index zero.
 *
 * @return `CX_OK` and the index in @p out, `CX_NOT_FOUND`, or
 *         `CX_INVALID_ARGUMENT` for a null @p out.
 */
[[nodiscard]] CX_API cx_status cx_slice_find(cx_slice haystack, cx_slice needle, size_t* out);

/**
 * @brief True when the slice has no bytes.
 */
[[nodiscard]] static inline bool cx_slice_empty(cx_slice slice) { return slice.length == 0; }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_SLICE_H */
