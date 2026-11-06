#ifndef CSTDX_STRING_VIEW_H
#define CSTDX_STRING_VIEW_H

/**
 * @file string_view.h
 * @brief Borrowed string view (cx_string_view).
 *
 * A view is a pointer plus a length. It does not own memory and it is not
 * guaranteed to be NUL-terminated. Use it for parsing and for passing string
 * data to functions that accept an explicit length.
 */

#include <stddef.h>
#include <stdint.h>

#include <cstdx/api.h>
#include <cstdx/status.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result of a string comparison.
 */
typedef enum cx_string_compare_result : int8_t {
    CX_STRING_LESS = -1,
    CX_STRING_EQUAL = 0,
    CX_STRING_GREATER = 1
} cx_string_compare_result;

/**
 * @brief A borrowed range of bytes.
 *
 * `data == nullptr` is only meaningful when `length == 0`.
 */
typedef struct cx_string_view {
    const char *data;
    size_t length;
} cx_string_view;

/**
 * @brief Build a view over a NUL-terminated string.
 *
 * @param cstr String to view, or nullptr for an empty view.
 */
[[nodiscard]] CX_API cx_string_view cx_string_view_from_cstr(
    const char *cstr);

/**
 * @brief Build a view from a pointer and a length.
 *
 * When @p data is nullptr the resulting length is forced to zero.
 */
[[nodiscard]] CX_API cx_string_view cx_string_view_from_parts(
    const char *data, size_t length);

/**
 * @brief Extract a sub-view.
 *
 * @param view View to slice.
 * @param start First byte to include. Must be at most `view.length`.
 * @param length Number of bytes to include.
 * @param out Receives the sub-view.
 * @return `CX_OK`, `CX_INVALID_ARGUMENT` for a null @p out, or
 *         `CX_OUT_OF_RANGE` when the range exceeds @p view.
 */
[[nodiscard]] CX_API cx_status cx_string_view_sub(cx_string_view view,
                                                  size_t start, size_t length,
                                                  cx_string_view *out);

/**
 * @brief Compare two views for equality.
 */
[[nodiscard]] CX_API bool cx_string_view_eq(cx_string_view left,
                                            cx_string_view right);

/**
 * @brief Order two views lexicographically by unsigned byte value.
 */
[[nodiscard]] CX_API cx_string_compare_result cx_string_view_compare(
    cx_string_view left, cx_string_view right);

/**
 * @brief Find the first occurrence of @p needle in @p haystack.
 *
 * An empty needle is found at index zero.
 *
 * @return `CX_OK` and the index in @p out, `CX_NOT_FOUND`, or
 *         `CX_INVALID_ARGUMENT` for a null @p out.
 */
[[nodiscard]] CX_API cx_status cx_string_view_find(cx_string_view haystack,
                                                   cx_string_view needle,
                                                   size_t *out);

/**
 * @brief Test whether @p view begins with @p prefix.
 */
[[nodiscard]] CX_API bool cx_string_view_starts_with(cx_string_view view,
                                                     cx_string_view prefix);

/**
 * @brief Test whether @p view ends with @p suffix.
 */
[[nodiscard]] CX_API bool cx_string_view_ends_with(cx_string_view view,
                                                   cx_string_view suffix);

/**
 * @brief Pull cursor for splitting a view by a delimiter.
 *
 * The cursor borrows the input and never allocates. An empty delimiter yields
 * the whole remaining input once. A trailing delimiter produces a final empty
 * piece.
 */
typedef struct cx_string_view_split {
    cx_string_view input;
    cx_string_view delimiter;
    size_t offset;
    bool done;
} cx_string_view_split;

/**
 * @brief Initialize a split cursor.
 */
CX_API void cx_string_view_split_init(cx_string_view_split *split,
                                      cx_string_view input,
                                      cx_string_view delimiter);

/**
 * @brief Produce the next piece.
 *
 * @return true when @p out was set, false when the cursor is exhausted.
 */
[[nodiscard]] CX_API bool cx_string_view_split_next(
    cx_string_view_split *split, cx_string_view *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_STRING_VIEW_H */
