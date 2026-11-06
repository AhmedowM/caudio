#ifndef CAUDIO_RUNTIME_PATH_H
#define CAUDIO_RUNTIME_PATH_H

/**
 * @file caudio/runtime/path.h
 * @brief Path manipulation utilities.
 *
 * All paths are UTF-8 encoded. Separators are normalized to '/' internally;
 * on Windows the native API uses '\\'.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Join @p base and @p child into a single path, writing at most
 *        @p out_size bytes (including NUL) into @p out.
 *
 * A '/' separator is inserted if needed. Returns the number of bytes written
 * (excluding NUL), or (size_t)-1 on truncation with @p err set.
 */
CAUDIO_API size_t ca_path_join(const char *base, const char *child, char *out,
                               size_t out_size, ca_error *err);

/**
 * @brief Return a pointer to the base name (after the last separator) of
 *        @p path, or @p path itself if there is no separator.
 */
CAUDIO_API const char *ca_path_basename(const char *path);

/**
 * @brief Return a pointer to the directory portion (up to the last
 *        separator) of @p path, or "." if there is no separator. The result
 *        points into @p path (not NUL-terminated on its own); the caller must
 *        copy it if a NUL-terminated string is needed.
 */
CAUDIO_API const char *ca_path_dirname(const char *path);

/**
 * @brief Normalize separators in-place: '\\' -> '/' on all platforms.
 *        Returns @p path.
 */
CAUDIO_API char *ca_path_normalize(char *path);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_PATH_H */
