#ifndef CSTDX_BYTES_H
#define CSTDX_BYTES_H

/**
 * @file bytes.h
 * @brief Owned, growable byte buffer (cx_bytes).
 *
 * A byte buffer is the binary counterpart of cx_string: it owns its storage,
 * is not NUL-terminated, and allows embedded zero bytes.
 *
 * Invariants for an initialized buffer:
 * - `length` is the number of valid bytes.
 * - `capacity` is the number of bytes the allocation can hold.
 * - `capacity >= length`.
 * - When `data != nullptr`, the allocation is `capacity` bytes.
 */

#include <cstdx/allocator.h>
#include <cstdx/api.h>
#include <cstdx/slice.h>
#include <cstdx/status.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An owned byte buffer.
 *
 * This is an opaque handle. Its layout is private to the library. Create one
 * with @ref cx_bytes_init or one of the `_from`/`_copy` constructors, and
 * release it with @ref cx_bytes_destroy.
 */
typedef struct cx_bytes cx_bytes;

/**
 * @brief Initialize an empty buffer.
 *
 * @param[out] out Receives a newly allocated handle on success. Set to
 *                 nullptr on failure.
 * @param allocator Allocator to use, or nullptr for the default allocator.
 *                  The same allocator owns the handle and the buffer.
 *
 * @return `CX_OK`, `CX_INVALID_ARGUMENT`, or `CX_OUT_OF_MEMORY`.
 */
[[nodiscard]] CX_API cx_status cx_bytes_init(cx_bytes** out, const cx_allocator* allocator);

/**
 * @brief Create a buffer from a block of bytes.
 *
 * @param[out] out Receives a newly allocated handle on success.
 * @param data Source bytes; may be nullptr when @p length is zero.
 * @param length Number of bytes to copy.
 * @param allocator Allocator to use, or nullptr for the default allocator.
 */
[[nodiscard]] CX_API cx_status cx_bytes_from(cx_bytes** out, const void* data, size_t length,
                                             const cx_allocator* allocator);

/**
 * @brief Create a buffer from a slice.
 */
[[nodiscard]] CX_API cx_status cx_bytes_from_slice(cx_bytes** out, cx_slice slice,
                                                   const cx_allocator* allocator);

/**
 * @brief Deep-copy a buffer into a new handle.
 *
 * @param[out] out Receives a newly allocated handle on success.
 * @param src Source buffer.
 *
 * The copy uses the source buffer's allocator.
 */
[[nodiscard]] CX_API cx_status cx_bytes_copy(cx_bytes** out, const cx_bytes* src);

/**
 * @brief Move the contents of @p src into the existing handle @p dst.
 *
 * @p dst keeps its identity but releases its current buffer; @p src is left
 * as a valid, empty buffer with its original allocator.
 */
CX_API void cx_bytes_move(cx_bytes* dst, cx_bytes* src);

/**
 * @brief Release the buffer handle and set @p out to nullptr.
 *
 * Passing nullptr, or a pointer to nullptr, is a no-op. A second destroy of
 * the same variable is therefore safe.
 */
CX_API void cx_bytes_destroy(cx_bytes** out);

/**
 * @brief Read-only pointer to a byte, or nullptr when out of range.
 */
[[nodiscard]] CX_API const uint8_t* cx_bytes_at(const cx_bytes* bytes, size_t index);

/**
 * @brief Mutable pointer to a byte, or nullptr when out of range.
 */
[[nodiscard]] CX_API uint8_t* cx_bytes_at_mut(cx_bytes* bytes, size_t index);

/**
 * @brief Read one byte by index.
 *
 * @return `CX_OK`, `CX_INVALID_ARGUMENT`, or `CX_OUT_OF_RANGE`.
 */
[[nodiscard]] CX_API cx_status cx_bytes_get(const cx_bytes* bytes, size_t index, uint8_t* value);

/**
 * @brief Replace one byte by index.
 *
 * @return `CX_OK`, `CX_INVALID_ARGUMENT`, or `CX_OUT_OF_RANGE`.
 */
[[nodiscard]] CX_API cx_status cx_bytes_set(cx_bytes* bytes, size_t index, uint8_t value);

/**
 * @brief Ensure capacity for at least @p min_capacity bytes.
 *
 * The buffer may grow by more than requested. The length is unchanged.
 */
[[nodiscard]] CX_API cx_status cx_bytes_reserve(cx_bytes* bytes, size_t min_capacity);

/**
 * @brief Resize the buffer, zero-filling newly exposed bytes.
 */
[[nodiscard]] CX_API cx_status cx_bytes_resize(cx_bytes* bytes, size_t new_length);

/**
 * @brief Replace the contents with a block of bytes.
 *
 * Aliasing is supported: @p data may point into @p bytes' own storage.
 */
[[nodiscard]] CX_API cx_status cx_bytes_assign(cx_bytes* bytes, const void* data, size_t length);

/**
 * @brief Replace the contents with a slice.
 *
 * Aliasing is supported.
 */
[[nodiscard]] CX_API cx_status cx_bytes_assign_slice(cx_bytes* bytes, cx_slice slice);

/**
 * @brief Set the length to zero and keep the allocation.
 */
CX_API void cx_bytes_clear(cx_bytes* bytes);

/**
 * @brief Append a block of bytes.
 *
 * Aliasing is supported.
 */
[[nodiscard]] CX_API cx_status cx_bytes_append(cx_bytes* bytes, const void* data, size_t length);

/**
 * @brief Append a slice.
 *
 * Aliasing is supported.
 */
[[nodiscard]] CX_API cx_status cx_bytes_append_slice(cx_bytes* bytes, cx_slice slice);

/**
 * @brief Append one byte.
 */
[[nodiscard]] CX_API cx_status cx_bytes_push_back(cx_bytes* bytes, uint8_t value);

/**
 * @brief Remove and optionally return the last byte.
 *
 * @param value Receives the byte, or nullptr when the caller only wants to
 *              remove it.
 * @return `CX_OK`, `CX_INVALID_ARGUMENT`, or `CX_OUT_OF_RANGE` when empty.
 */
[[nodiscard]] CX_API cx_status cx_bytes_pop_back(cx_bytes* bytes, uint8_t* value);

/**
 * @brief Insert a block of bytes at @p index.
 *
 * @p index may equal the current length. Aliasing is supported.
 */
[[nodiscard]] CX_API cx_status cx_bytes_insert(cx_bytes* bytes, size_t index, const void* data,
                                               size_t length);

/**
 * @brief Remove @p count bytes starting at @p index.
 *
 * @p count is clamped to the remaining length.
 */
[[nodiscard]] CX_API cx_status cx_bytes_erase(cx_bytes* bytes, size_t index, size_t count);

/**
 * @brief Replace @p count bytes at @p index with a block of bytes.
 *
 * @p index may equal the current length, in which case this appends.
 * @p count is clamped. Aliasing is supported.
 */
[[nodiscard]] CX_API cx_status cx_bytes_replace(cx_bytes* bytes, size_t index, size_t count,
                                                const void* data, size_t length);

/**
 * @brief Swap two buffers, including their allocators.
 */
CX_API void cx_bytes_swap(cx_bytes* left, cx_bytes* right);

/**
 * @brief Length in bytes, or zero when @p bytes is nullptr.
 */
[[nodiscard]] CX_API size_t cx_bytes_length(const cx_bytes* bytes);

/**
 * @brief Capacity in bytes, or zero when @p bytes is nullptr.
 */
[[nodiscard]] CX_API size_t cx_bytes_capacity(const cx_bytes* bytes);

/**
 * @brief Borrowed pointer to the storage.
 *
 * Returns nullptr for a null or unallocated buffer.
 */
[[nodiscard]] CX_API const uint8_t* cx_bytes_data(const cx_bytes* bytes);

/**
 * @brief True when the buffer is null or has no bytes.
 */
[[nodiscard]] CX_API bool cx_bytes_empty(const cx_bytes* bytes);

/**
 * @brief True when @p bytes is nullptr or owns no storage.
 */
[[nodiscard]] CX_API bool cx_bytes_is_null(const cx_bytes* bytes);

/**
 * @brief Compare two buffers for equality.
 */
[[nodiscard]] CX_API bool cx_bytes_equals(const cx_bytes* left, const cx_bytes* right);

/**
 * @brief Borrow a slice over @p bytes.
 */
[[nodiscard]] CX_API cx_slice cx_bytes_as_slice(const cx_bytes* bytes);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_BYTES_H */
