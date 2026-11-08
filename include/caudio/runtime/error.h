#ifndef CA_RUNTIME_ERROR_H
#define CA_RUNTIME_ERROR_H

/**
 * @file ca_runtime/error.h
 * @brief Fixed-size error record for caudio runtime APIs.
 *
 * Every fallible ca_runtime function takes an optional `ca_error *err`. When
 * non-nullptr it is zeroed on success and initialized on failure. The 256-byte
 * fixed size keeps the ABI stable and avoids heap allocation for error text.
 */

#include <stddef.h>

#include <caudio/api.h>
#include <caudio/runtime/status.h>

#ifdef __cplusplus
extern "C" {
#endif

constexpr size_t CA_ERROR_MAX = 256;

/**
 * @brief Fixed-size error record.
 */
typedef struct ca_error {
    ca_status code;
    char message[CA_ERROR_MAX];
} ca_error;

/**
 * @brief Initialize an error record to the success state.
 */
CAUDIO_API void ca_error_init(ca_error *err);

/**
 * @brief Set an error record from a status code and a printf-style message.
 *
 * Safe truncation: the message is always NUL-terminated.
 */
CAUDIO_API void ca_error_set(ca_error *err, ca_status code, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* CA_RUNTIME_ERROR_H */
