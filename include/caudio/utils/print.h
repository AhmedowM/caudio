#ifndef CAUDIO_UTILS_PRINT_H
#define CAUDIO_UTILS_PRINT_H

/**
 * @file caudio/utils/print.h
 * @brief Portable print/println facade over stdio.
 * @ingroup caudio_utils
 * @details Routes through `fprintf`/`fputs` on every toolchain.
 * Always call `ca_print`/`ca_println`, never `fprintf` directly, so
 * output is consistent across platforms.
 */

#include <caudio/api.h>
#include <cstdx/string_view.h>

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Formats arguments and writes them to a C stream.
 *
 * @param f Destination `FILE*`; must be non-null and writable.
 * @param fmt `printf`-style format string.
 * @param ... Values consumed by the format string.
 */
CAUDIO_API void ca_print(FILE* f, const char* fmt, ...)
    CA_PRINTF(2, 3);

/**
 * @brief Formats arguments, writes them to a C stream, and appends a newline.
 *
 * @param f Destination `FILE*`; must be non-null and writable.
 * @param fmt `printf`-style format string.
 * @param ... Values consumed by the format string.
 */
CAUDIO_API void ca_println(FILE* f, const char* fmt, ...)
    CA_PRINTF(2, 3);

/**
 * @brief Formats arguments and writes them to `stdout`.
 *
 * @param fmt `printf`-style format string.
 * @param ... Values consumed by the format string.
 */
CAUDIO_API void ca_print_stdout(const char* fmt, ...)
    CA_PRINTF(1, 2);

/**
 * @brief Formats arguments, writes them to `stdout`, and appends a newline.
 *
 * @param fmt `printf`-style format string.
 * @param ... Values consumed by the format string.
 */
CAUDIO_API void ca_println_stdout(const char* fmt, ...)
    CA_PRINTF(1, 2);

/**
 * @brief Writes a pre-formatted string to a C stream.
 *
 * @param f Destination `FILE*`.
 * @param s String view to write.
 * @return Number of bytes written, or negative on error.
 */
CAUDIO_API long ca_print_view(FILE* f, cx_string_view s);

/**
 * @brief Writes a pre-formatted string to a C stream, followed by a newline.
 *
 * @param f Destination `FILE*`.
 * @param s String view to write.
 * @return Number of bytes written, or negative on error.
 */
CAUDIO_API long ca_println_view(FILE* f, cx_string_view s);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_UTILS_PRINT_H */
