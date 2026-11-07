#ifndef CAUDIO_RUNTIME_ENDPOINT_H
#define CAUDIO_RUNTIME_ENDPOINT_H

/**
 * @file caudio/runtime/endpoint.h
 * @brief Local IPC endpoints over UNIX-domain sockets.
 *
 * POSIX uses a filesystem AF_UNIX stream socket. Windows uses AF_UNIX as well,
 * which requires Windows 10 version 1803 or newer; the name is still a
 * filesystem path. Both sides of a connection are represented by the same
 * opaque ca_endpoint handle.
 *
 * A listening endpoint owns its socket path: ca_endpoint_close removes the
 * path after closing the socket. Accepted and connected endpoints do not own
 * any path. Stale socket files left by a crashed process must be removed by
 * the caller before listening again.
 *
 * Blocking I/O is used throughout. ca_endpoint_accept and
 * ca_endpoint_connect are the only operations with a timeout.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque endpoint handle.
 */
typedef struct ca_endpoint ca_endpoint;

#define CA_ENDPOINT_INVALID ((ca_endpoint *)nullptr)

/**
 * @brief Create a listening endpoint bound to @p name.
 *
 * @param out     Receives the listening endpoint.
 * @param name    UTF-8 filesystem path for the socket. The platform's
 *                `sun_path` limit applies (108 bytes including the NUL on
 *                Linux and Windows, 104 on macOS).
 * @param backlog Listen backlog; values <= 0 select the system maximum.
 * @param err     Optional error record.
 * @return CA_OK on success, CA_ALREADY_EXISTS if the path is in use, or
 *         another ca_status.
 */
CAUDIO_API ca_status ca_endpoint_listen(ca_endpoint **out, const char *name,
                                        int backlog, ca_error *err);

/**
 * @brief Accept a client connection on a listening endpoint.
 *
 * @param server     Listening endpoint.
 * @param timeout_ms Negative to wait indefinitely, 0 to poll, positive for a
 *                   bounded wait in milliseconds.
 * @param out        Receives the connected endpoint.
 * @param err        Optional error record.
 * @return CA_OK on success, CA_TIMED_OUT if no client arrived in time, or
 *         another ca_status.
 */
CAUDIO_API ca_status ca_endpoint_accept(ca_endpoint *server, int timeout_ms,
                                        ca_endpoint **out, ca_error *err);

/**
 * @brief Connect to a listening endpoint at @p name.
 *
 * With a positive timeout, transient failures (the server is not listening
 * yet) are retried with a short backoff until the deadline. A timeout of 0
 * makes a single attempt. A negative timeout retries indefinitely.
 *
 * @param out        Receives the connected endpoint.
 * @param name       UTF-8 filesystem path of the socket.
 * @param timeout_ms Negative to retry forever, 0 for one attempt, positive for
 *                   a deadline in milliseconds.
 * @param err        Optional error record.
 * @return CA_OK on success, CA_TIMED_OUT if the deadline elapsed, or another
 *         ca_status.
 */
CAUDIO_API ca_status ca_endpoint_connect(ca_endpoint **out, const char *name,
                                         int timeout_ms, ca_error *err);

/**
 * @brief Read up to @p size bytes.
 *
 * @return Bytes read (0 on orderly shutdown), or (size_t)-1 on error with
 *         @p err set.
 */
CAUDIO_API size_t ca_endpoint_read(ca_endpoint *endpoint, void *buffer,
                                   size_t size, ca_error *err);

/**
 * @brief Write exactly @p size bytes.
 *
 * Partial writes are retried internally. @p buffer may be NULL only when
 * @p size is 0.
 *
 * @return @p size on success, or (size_t)-1 on error with @p err set.
 */
CAUDIO_API size_t ca_endpoint_write(ca_endpoint *endpoint, const void *buffer,
                                    size_t size, ca_error *err);

/**
 * @brief Flush pending output.
 *
 * UNIX-domain sockets have no user-space output buffer, so this is a
 * successful no-op. It exists so pipe-based backends can share the API.
 */
CAUDIO_API ca_status ca_endpoint_flush(ca_endpoint *endpoint, ca_error *err);

/**
 * @brief Close @p endpoint and set it to CA_ENDPOINT_INVALID.
 *
 * A listening endpoint removes its socket path. NULL and *endpoint == NULL are
 * no-ops.
 */
CAUDIO_API void ca_endpoint_close(ca_endpoint **endpoint);

/**
 * @brief Return the endpoint's socket path, or NULL for an accepted endpoint.
 */
CAUDIO_API const char *ca_endpoint_name(const ca_endpoint *endpoint);

/**
 * @brief Return true if @p endpoint is a listening endpoint.
 */
CAUDIO_API bool ca_endpoint_is_server(const ca_endpoint *endpoint);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_ENDPOINT_H */
