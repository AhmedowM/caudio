#ifndef CSTDX_STRING_BUILDER_H
#define CSTDX_STRING_BUILDER_H

/**
 * @file string_builder.h
 * @brief Growable string builder (cx_string_builder).
 *
 * The builder is a thin, append-oriented facade over cx_string. It is the
 * place for formatting and incremental construction; use cx_string directly
 * when random access or explicit reserve/resize is needed.
 */

#include <cstdx/allocator.h>
#include <cstdx/api.h>
#include <cstdx/status.h>
#include <cstdx/string.h>
#include <cstdx/string_view.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Builder state.
 *
 * This is an opaque handle. Its layout is private to the library.
 */
typedef struct cx_string_builder cx_string_builder;

/**
 * @brief Initialize an empty builder.
 *
 * @param[out] out Receives a newly allocated handle on success. Set to
 *                 nullptr on failure.
 * @param allocator Allocator to use, or nullptr for the default allocator.
 *
 * @return `CX_OK`, `CX_INVALID_ARGUMENT`, or `CX_OUT_OF_MEMORY`.
 */
[[nodiscard]] CX_API cx_status cx_string_builder_init(cx_string_builder** out,
                                                      const cx_allocator* allocator);

/**
 * @brief Release the builder handle and set @p out to nullptr.
 *
 * Passing nullptr, or a pointer to nullptr, is a no-op. A second destroy of
 * the same variable is therefore safe.
 */
CX_API void cx_string_builder_destroy(cx_string_builder** out);

/**
 * @brief Append a view.
 */
[[nodiscard]] CX_API cx_status cx_string_builder_append(cx_string_builder* builder,
                                                        cx_string_view view);

/**
 * @brief Append a NUL-terminated string.
 */
[[nodiscard]] CX_API cx_status cx_string_builder_append_cstr(cx_string_builder* builder,
                                                             const char* cstr);

/**
 * @brief Append one byte.
 */
[[nodiscard]] CX_API cx_status cx_string_builder_append_char(cx_string_builder* builder,
                                                             char value);

/**
 * @brief Append formatted output.
 *
 * Uses the C `printf` format language.
 */
CX_API CX_PRINTF(2, 3) cx_status
    cx_string_builder_printf(cx_string_builder* builder, const char* format, ...);

/**
 * @brief Borrowed view over the current contents.
 */
[[nodiscard]] CX_API cx_string_view cx_string_builder_view(const cx_string_builder* builder);

/**
 * @brief Borrowed NUL-terminated contents.
 */
[[nodiscard]] CX_API const char* cx_string_builder_cstr(const cx_string_builder* builder);

/**
 * @brief Current length in bytes.
 */
[[nodiscard]] CX_API size_t cx_string_builder_length(const cx_string_builder* builder);

/**
 * @brief Move the contents into a new string handle and leave the builder
 *        empty.
 *
 * @param[out] out Receives a newly allocated string handle on success. Set
 *                 to nullptr on failure.
 *
 * The builder keeps its allocator and can be used again.
 */
[[nodiscard]] CX_API cx_status cx_string_builder_finish(cx_string_builder* builder,
                                                        cx_string** out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_STRING_BUILDER_H */
