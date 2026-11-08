#ifndef CAUDIO_RUNTIME_RWLOCK_H
#define CAUDIO_RUNTIME_RWLOCK_H

/**
 * @file caudio/runtime/rwlock.h
 * @brief Read-write lock.
 *
 * Multiple readers may hold the lock concurrently; a writer holds it
 * exclusively. POSIX: pthread_rwlock_t. Win32: SRWLOCK.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque read-write lock handle.
 */
typedef struct ca_rwlock *ca_rwlock_t;

/**
 * @brief Create a read-write lock.
 *
 * @param out Receives the new lock handle.
 * @param err Optional error record.
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_rwlock_create(ca_rwlock_t *out, ca_error *err);

/**
 * @brief Destroy a read-write lock. Must not be held or waited on.
 *
 * Passing nullptr is a no-op.
 */
CAUDIO_API void ca_rwlock_destroy(ca_rwlock_t rwlock);

/**
 * @brief Acquire the read lock, blocking until available.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_rwlock_rdlock(ca_rwlock_t rwlock, ca_error *err);

/**
 * @brief Acquire the write lock, blocking until available.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_rwlock_wrlock(ca_rwlock_t rwlock, ca_error *err);

/**
 * @brief Release the read or write lock. Must be held by the calling thread.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_rwlock_unlock(ca_rwlock_t rwlock, ca_error *err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_RWLOCK_H */
