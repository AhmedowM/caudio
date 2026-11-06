#ifndef CA_RUNTIME_STATUS_H
#define CA_RUNTIME_STATUS_H

/**
 * @file ca_runtime/status.h
 * @brief caudio runtime status codes.
 *
 * Self-contained: ca_runtime does not depend on cstdx. Its status codes are
 * wire-stable and form part of the IPC / JSON / on-disk contract.
 */

#include <stdint.h>

#include <caudio/api.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Runtime status codes.
 *
 * CA_OK == 0. Negative values are errors. The numeric values and
 * ca_status_name() spellings are part of the wire contract and must not
 * change.
 */
typedef enum ca_status : uint8_t {
    CA_OK = 0,

    /* Generic errors (1..31). */
    CA_INVALID_ARGUMENT = 1,
    CA_OUT_OF_MEMORY = 2,
    CA_NOT_FOUND = 3,
    CA_ALREADY_EXISTS = 4,
    CA_IO_ERROR = 5,
    CA_PERMISSION_DENIED = 6,
    CA_TIMED_OUT = 7,
    CA_INTERRUPTED = 8,
    CA_OVERFLOW = 9,
    CA_NOT_SUPPORTED = 10,
    CA_BUSY = 11,
    CA_CANCELLED = 12,

    /* ca_runtime specific errors (64..127). */
    CA_RUNTIME_BASE = 64,
    CA_JSON_ERROR = CA_RUNTIME_BASE + 0,
    CA_PATH_ERROR = CA_RUNTIME_BASE + 1,
    CA_THREAD_ERROR = CA_RUNTIME_BASE + 2,
    CA_LOCK_ERROR = CA_RUNTIME_BASE + 3,
    CA_ENDPOINT_ERROR = CA_RUNTIME_BASE + 4,
    CA_SOCKET_ERROR = CA_RUNTIME_BASE + 5,
    CA_SHM_ERROR = CA_RUNTIME_BASE + 6,
    CA_FS_ERROR = CA_RUNTIME_BASE + 7,
} ca_status;

/**
 * @brief Return a human-readable name for a ca_status value.
 * @return Static string; never NULL. Unknown values return "ca_status_unknown".
 */
[[nodiscard]] CAUDIO_API const char *ca_status_name(ca_status status);

#ifdef __cplusplus
}
#endif

#endif /* CA_RUNTIME_STATUS_H */
