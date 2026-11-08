#ifndef CAUDIO_RUNTIME_ENV_H
#define CAUDIO_RUNTIME_ENV_H

/**
 * @file caudio/runtime/env.h
 * @brief Process environment variables.
 *
 * Values are UTF-8 on every platform. On Windows, names and values are
 * converted to UTF-16 for the native environment API.
 *
 * The environment is process-global. POSIX getenv/setenv/unsetenv are not
 * synchronized against each other, so callers must serialize environment
 * mutation and lookup if several threads use these functions concurrently.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Look up @p name and copy its value into @p out.
 *
 * Passing @c out = nullptr with @c out_size = 0 queries the value length without
 * copying it; @p out_len receives the length in bytes, excluding the NUL.
 *
 * When @p out is non-nullptr, at least @p out_size bytes are available. If the
 * value does not fit, CA_OVERFLOW is returned, @p out is set to an empty
 * string, and @p out_len receives the required length (excluding NUL).
 *
 * @param name    NUL-terminated UTF-8 variable name. Must not be empty or
 *                contain '='.
 * @param out     Destination buffer, or nullptr for a length query.
 * @param out_size Size of @p out in bytes.
 * @param out_len Optional; receives the copied length or 0 on failure.
 * @param err     Optional error record.
 * @return CA_OK on success, CA_NOT_FOUND if the variable is unset,
 *         CA_OVERFLOW if the buffer is too small, or another ca_status.
 */
CAUDIO_API ca_status ca_env_get(const char *name, char *out, size_t out_size,
                                size_t *out_len, ca_error *err);

/**
 * @brief Set @p name to @p value.
 *
 * @param name      UTF-8 variable name.
 * @param value     UTF-8 value; an empty string is a valid value.
 * @param overwrite If false and the variable already exists, return
 *                  CA_ALREADY_EXISTS without changing it.
 * @param err       Optional error record.
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_env_set(const char *name, const char *value,
                                bool overwrite, ca_error *err);

/**
 * @brief Remove @p name from the environment.
 *
 * Removing an unset variable is not an error.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_env_unset(const char *name, ca_error *err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_ENV_H */
