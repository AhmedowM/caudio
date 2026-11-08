#ifndef CAUDIO_RUNTIME_MUTEX_H
#define CAUDIO_RUNTIME_MUTEX_H

/**
 * @file caudio/runtime/mutex.h
 * @brief Mutex (mutual exclusion) primitive.
 *
 * Non-recursive, non-error-checking by default. POSIX: pthread_mutex_t.
 * Win32: SRWLOCK (Vista+) or CRITICAL_SECTION fallback.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque mutex handle.
 */
typedef struct ca_mutex *ca_mutex_t;

/**
 * @brief Create a mutex.
 *
 * @param out Receives the new mutex handle.
 * @param err Optional error record.
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_mutex_create(ca_mutex_t *out, ca_error *err);

/**
 * @brief Destroy a mutex. Must not be locked or in use by another thread.
 *
 * Passing nullptr is a no-op.
 */
CAUDIO_API void ca_mutex_destroy(ca_mutex_t mutex);

/**
 * @brief Lock @p mutex, blocking until available.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_mutex_lock(ca_mutex_t mutex, ca_error *err);

/**
 * @brief Try to lock @p mutex without blocking.
 *
 * @return CA_OK if acquired, CA_BUSY if already held by another thread.
 */
CAUDIO_API ca_status ca_mutex_trylock(ca_mutex_t mutex, ca_error *err);

/**
 * @brief Unlock @p mutex. Must be held by the calling thread.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_mutex_unlock(ca_mutex_t mutex, ca_error *err);

/**
 * @brief Return the native synchronization primitive handle.
 *
 * POSIX: returns a pointer to the pthread_mutex_t.
 * Win32: returns a pointer to the SRWLOCK.
 *
 * Intended for use by condition-variable wait primitives in this library.
 * Not part of the stable public ABI; may change between minor versions.
 */
CAUDIO_API void *ca_mutex_native_handle(ca_mutex_t mutex);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_MUTEX_H */
