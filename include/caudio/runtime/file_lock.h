#ifndef CAUDIO_RUNTIME_FILE_LOCK_H
#define CAUDIO_RUNTIME_FILE_LOCK_H

/**
 * @file caudio/runtime/file_lock.h
 * @brief File region locking.
 *
 * POSIX backend: fcntl F_SETLK/F_GETLK. Win32 backend: LockFileEx/UnlockFileEx.
 * All functions return ca_status and take an optional ca_error *err.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Lock type.
 */
typedef enum ca_file_lock_type {
    CA_FILE_LOCK_READ = 0,  /* shared lock */
    CA_FILE_LOCK_WRITE = 1, /* exclusive lock */
} ca_file_lock_type;

/**
 * @brief Lock a region of an open file.
 *
 * @param fd        Platform file descriptor / handle.
 * @param type      Lock type (shared or exclusive).
 * @param offset    Start of the region (bytes).
 * @param length    Length of the region (bytes).
 * @param block     If nonzero, block until the lock is available.
 * @param err       Optional error record.
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_file_lock(void* fd, ca_file_lock_type type, int64_t offset, int64_t length,
                                  int block, ca_error* err);

/**
 * @brief Unlock a previously locked region.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_file_unlock(void* fd, int64_t offset, int64_t length, ca_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_FILE_LOCK_H */
