#ifndef CAUDIO_RUNTIME_SHM_H
#define CAUDIO_RUNTIME_SHM_H

/**
 * @file caudio/runtime/shm.h
 * @brief Shared memory segments.
 *
 * POSIX backend: shm_open/mmap/shm_unlink. Win32 backend: CreateFileMapping
 * / MapViewOfFile / UnmapViewOfFile.
 * All functions return ca_status and take an optional ca_error *err.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque shared memory segment handle.
 */
typedef struct ca_shm *ca_shm_t;

#define CA_SHM_INVALID ((ca_shm_t)nullptr)

/**
 * @brief Create or open a shared memory segment.
 *
 * @param name    Segment name (POSIX: prefixed with "/"; Win32: named object).
 * @param size    Size in bytes. If 0 and the segment already exists, its
 *                current size is used.
 * @param create  If nonzero, create the segment if it does not exist.
 * @param err     Optional error record.
 * @return New segment handle, or CA_SHM_INVALID on failure.
 */
CAUDIO_API ca_shm_t ca_shm_open(const char *name, size_t size, int create,
                                ca_error *err);

/**
 * @brief Map the segment into the caller's address space.
 *
 * @param offset  Offset within the segment (must be page-aligned).
 * @param length  Number of bytes to map, or 0 to map the rest.
 * @param out     Receives the mapped address.
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_shm_map(ca_shm_t shm, size_t offset, size_t length,
                                void **out, ca_error *err);

/**
 * @brief Unmap a previously mapped region.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_shm_unmap(void *addr, size_t length, ca_error *err);

/**
 * @brief Close the segment handle. The underlying memory persists until
 *        unlinked.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_shm_close(ca_shm_t shm, ca_error *err);

/**
 * @brief Unlink (destroy) a named segment. The memory is freed once all
 *        handles are closed.
 *
 * @return CA_OK on success.
 */
CAUDIO_API ca_status ca_shm_unlink(const char *name, ca_error *err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_RUNTIME_SHM_H */
