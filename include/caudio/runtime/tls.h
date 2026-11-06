#ifndef CAUDIO_RUNTIME_TLS_H
#define CAUDIO_RUNTIME_TLS_H

/**
 * @file caudio/runtime/tls.h
 * @brief Thread-local storage (key/value slots).
 *
 * Each thread can bind a distinct pointer value to a key. POSIX:
 * pthread_key_t. Win32: TlsAlloc / TlsGetValue / TlsSetValue.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque TLS key handle.
 */
typedef struct ca_tls_key *ca_tls_key_t;

/**
 * @brief TLS destructor signature. Called with the thread's value when the
 *        thread exits (if non-NULL).
 */
typedef void (*ca_tls_destructor)(void *value);

/**
 * @brief Create a TLS key. @p destructor, if non-NULL, is called with the
 *        thread-local value when a thread exits.
 *
 * @param out   Receives the new key handle.
 * @param dest  Optional destructor.
 * @param err   Optional error record.
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_tls_key_create(ca_tls_key_t *out,
                                       ca_tls_destructor dest, ca_error *err);

/**
 * @brief Destroy a TLS key. Does not invoke destructors for existing values.
 *
 * Passing NULL is a no-op.
 */
CAUDIO_API void ca_tls_key_destroy(ca_tls_key_t key);

/**
 * @brief Set the value bound to @p key for the calling thread.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_tls_set(ca_tls_key_t key, void *value,
                                ca_error *err);

/**
 * @brief Get the value bound to @p key for the calling thread, or NULL if none
 *        has been set.
 *
 * @return Thread-local value, or NULL.
 */
CAUDIO_API void *ca_tls_get(ca_tls_key_t key, ca_error *err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_TLS_H */
