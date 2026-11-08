#ifndef CSTDX_STRING_H
#define CSTDX_STRING_H

/**
 * @file string.h
 * @brief Owned, growable, NUL-terminated byte string (cx_string).
 *
 * Invariants for an initialized string:
 * - `length` is the number of content bytes.
 * - `capacity` is the number of content bytes the buffer can hold, excluding
 *   the terminating NUL. `capacity >= length`.
 * - When `data != nullptr`, `data[length] == '\0'` and the allocation is
 *   `capacity + 1` bytes.
 *
 * Embedded NUL bytes are allowed. `cx_string_cstr()` consumers then see a
 * truncated value, while `cx_string_length()` remains authoritative.
 */

#include <cstdx/allocator.h>
#include <cstdx/api.h>
#include <cstdx/status.h>
#include <cstdx/string_view.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An owned byte string.
 *
 * This is an opaque handle. Its layout is private to the library, so callers
 * cannot accidentally break the length/capacity invariants. Create one with
 * @ref cx_string_init or one of the `_from`/`_copy` constructors, and release
 * it with @ref cx_string_destroy.
 */
typedef struct cx_string cx_string;

/**
 * @brief Initialize an empty string.
 *
 * @param[out] out Receives a newly allocated handle on success. Set to
 *                 nullptr on failure.
 * @param allocator Allocator to use, or nullptr for the default allocator.
 *                  The same allocator owns the handle and the buffer.
 *
 * @return `CX_OK`, `CX_INVALID_ARGUMENT`, or `CX_OUT_OF_MEMORY`.
 */
[[nodiscard]] CX_API cx_status cx_string_init(cx_string** out, const cx_allocator* allocator);

/**
 * @brief Create a string from a NUL-terminated string.
 *
 * @param[out] out Receives a newly allocated handle on success.
 * @param cstr Source bytes. Must not be nullptr.
 * @param allocator Allocator to use, or nullptr for the default allocator.
 */
[[nodiscard]] CX_API cx_status cx_string_from(cx_string** out, const char* cstr,
                                              const cx_allocator* allocator);

/**
 * @brief Create a string from a view.
 *
 * @param[out] out Receives a newly allocated handle on success.
 * @param view Source bytes.
 * @param allocator Allocator to use, or nullptr for the default allocator.
 */
[[nodiscard]] CX_API cx_status cx_string_from_view(cx_string** out, cx_string_view view,
                                                   const cx_allocator* allocator);

/**
 * @brief Deep-copy a string into a new handle.
 *
 * @param[out] out Receives a newly allocated handle on success.
 * @param src Source string.
 *
 * The copy uses the source string's allocator.
 */
[[nodiscard]] CX_API cx_status cx_string_copy(cx_string** out, const cx_string* src);

/**
 * @brief Move the contents of @p src into the existing handle @p dst.
 *
 * @p dst keeps its identity but releases its current buffer; @p src is left
 * as a valid, empty string with its original allocator.
 */
CX_API void cx_string_move(cx_string* dst, cx_string* src);

/**
 * @brief Release the string handle and set @p out to nullptr.
 *
 * Passing nullptr, or a pointer to nullptr, is a no-op. A second destroy of
 * the same variable is therefore safe.
 */
CX_API void cx_string_destroy(cx_string** out);

/**
 * @brief Read-only pointer to a byte, or nullptr when out of range.
 */
[[nodiscard]] CX_API const char* cx_string_at(const cx_string* str, size_t index);

/**
 * @brief Mutable pointer to a byte, or nullptr when out of range.
 */
[[nodiscard]] CX_API char* cx_string_at_mut(cx_string* str, size_t index);

/**
 * @brief Read one byte by index.
 *
 * @return `CX_OK`, `CX_INVALID_ARGUMENT`, or `CX_OUT_OF_RANGE`.
 */
[[nodiscard]] CX_API cx_status cx_string_get(const cx_string* str, size_t index, char* value);

/**
 * @brief Replace one byte by index.
 *
 * @return `CX_OK`, `CX_INVALID_ARGUMENT`, or `CX_OUT_OF_RANGE`.
 */
[[nodiscard]] CX_API cx_status cx_string_set(cx_string* str, size_t index, char value);

/**
 * @brief Ensure capacity for at least @p min_capacity content bytes.
 *
 * The buffer may grow by more than requested. The length is unchanged.
 */
[[nodiscard]] CX_API cx_status cx_string_reserve(cx_string* str, size_t min_capacity);

/**
 * @brief Resize the string, zero-filling newly exposed bytes.
 */
[[nodiscard]] CX_API cx_status cx_string_resize(cx_string* str, size_t new_length);

/**
 * @brief Replace the contents with a NUL-terminated string.
 *
 * Aliasing is supported: @p cstr may point into @p str's own buffer.
 */
[[nodiscard]] CX_API cx_status cx_string_assign(cx_string* str, const char* cstr);

/**
 * @brief Replace the contents with a view.
 *
 * Aliasing is supported.
 */
[[nodiscard]] CX_API cx_status cx_string_assign_view(cx_string* str, cx_string_view view);

/**
 * @brief Set the length to zero and keep the allocation.
 */
CX_API void cx_string_clear(cx_string* str);

/**
 * @brief Append another string.
 *
 * Aliasing is supported.
 */
[[nodiscard]] CX_API cx_status cx_string_append(cx_string* str, const cx_string* other);

/**
 * @brief Append a NUL-terminated string.
 *
 * Aliasing is supported.
 */
[[nodiscard]] CX_API cx_status cx_string_append_cstr(cx_string* str, const char* cstr);

/**
 * @brief Append a view.
 *
 * Aliasing is supported.
 */
[[nodiscard]] CX_API cx_status cx_string_append_view(cx_string* str, cx_string_view view);

/**
 * @brief Append one byte.
 */
[[nodiscard]] CX_API cx_status cx_string_push_back(cx_string* str, char value);

/**
 * @brief Remove and optionally return the last byte.
 *
 * @param value Receives the byte, or nullptr when the caller only wants to
 *              remove it.
 * @return `CX_OK`, `CX_INVALID_ARGUMENT`, or `CX_OUT_OF_RANGE` when empty.
 */
[[nodiscard]] CX_API cx_status cx_string_pop_back(cx_string* str, char* value);

/**
 * @brief Insert @p view at @p index.
 *
 * @p index may equal the current length. Aliasing is supported.
 */
[[nodiscard]] CX_API cx_status cx_string_insert(cx_string* str, size_t index, cx_string_view view);

/**
 * @brief Remove @p count bytes starting at @p index.
 *
 * @p count is clamped to the remaining length.
 */
[[nodiscard]] CX_API cx_status cx_string_erase(cx_string* str, size_t index, size_t count);

/**
 * @brief Replace @p count bytes at @p index with @p view.
 *
 * @p index may equal the current length, in which case this appends.
 * @p count is clamped. Aliasing is supported.
 */
[[nodiscard]] CX_API cx_status cx_string_replace(cx_string* str, size_t index, size_t count,
                                                 cx_string_view view);

/**
 * @brief Swap two strings, including their allocators.
 */
CX_API void cx_string_swap(cx_string* left, cx_string* right);

/**
 * @brief Find the first occurrence of @p needle.
 *
 * @return `CX_OK` and the index in @p index, `CX_NOT_FOUND`, or
 *         `CX_INVALID_ARGUMENT`.
 */
[[nodiscard]] CX_API cx_status cx_string_find(const cx_string* str, cx_string_view needle,
                                              size_t* index);

/**
 * @brief Find the first occurrence of a NUL-terminated needle.
 */
[[nodiscard]] CX_API cx_status cx_string_find_cstr(const cx_string* str, const char* needle,
                                                   size_t* index);

/**
 * @brief Test whether @p needle occurs in @p str.
 */
[[nodiscard]] CX_API bool cx_string_contains(const cx_string* str, cx_string_view needle);

/**
 * @brief Test whether @p str begins with @p prefix.
 */
[[nodiscard]] CX_API bool cx_string_starts_with(const cx_string* str, cx_string_view prefix);

/**
 * @brief Test whether @p str ends with @p suffix.
 */
[[nodiscard]] CX_API bool cx_string_ends_with(const cx_string* str, cx_string_view suffix);

/**
 * @brief Length in bytes, or zero when @p str is nullptr.
 */
[[nodiscard]] CX_API size_t cx_string_length(const cx_string* str);

/**
 * @brief Capacity in bytes, or zero when @p str is nullptr.
 */
[[nodiscard]] CX_API size_t cx_string_capacity(const cx_string* str);

/**
 * @brief Borrowed NUL-terminated pointer.
 *
 * Returns "" for a null or unallocated string.
 */
[[nodiscard]] CX_API const char* cx_string_cstr(const cx_string* str);

/**
 * @brief True when the string is null or has no bytes.
 */
[[nodiscard]] CX_API bool cx_string_empty(const cx_string* str);

/**
 * @brief True when @p str is nullptr or owns no buffer.
 */
[[nodiscard]] CX_API bool cx_string_is_null(const cx_string* str);

/**
 * @brief Compare two strings for equality.
 */
[[nodiscard]] CX_API bool cx_string_equals(const cx_string* left, const cx_string* right);

/**
 * @brief Compare a string with a NUL-terminated string for equality.
 */
[[nodiscard]] CX_API bool cx_string_equals_cstr(const cx_string* str, const char* cstr);

/**
 * @brief Order two strings lexicographically by unsigned byte value.
 */
[[nodiscard]] CX_API cx_string_compare_result cx_string_compare(const cx_string* left,
                                                                const cx_string* right);

/**
 * @brief Order a string and a NUL-terminated string.
 */
[[nodiscard]] CX_API cx_string_compare_result cx_string_compare_cstr(const cx_string* str,
                                                                     const char* cstr);

/**
 * @brief Borrow a view over @p str.
 */
[[nodiscard]] CX_API cx_string_view cx_string_view_of(const cx_string* str);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_STRING_H */
