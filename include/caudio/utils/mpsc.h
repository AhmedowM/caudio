#ifndef CAUDIO_UTILS_MPSC_H
#define CAUDIO_UTILS_MPSC_H

/**
 * @file caudio/utils/mpsc.h
 * @brief Multi-producer single-consumer bounded queue.
 * @ingroup caudio_utils
 * @details Thread-safe bounded queue. Multiple producers may push; exactly
 * one consumer may pop. Uses a mutex + condition variable.
 */

#include <caudio/api.h>
#include <cstdx/allocator.h>

#include <caudio/runtime/error.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque MPSC queue handle.
 */
typedef struct ca_mpsc ca_mpsc;

/**
 * @brief Create a queue.
 *
 * @param[out] out Receives the new queue handle on success.
 * @param capacity Maximum number of elements (0 is normalized to 1).
 * @param elem_size Size of each element in bytes.
 * @param alloc Allocator for storage, or nullptr for default.
 * @return CA_OK on success, CA_OUT_OF_MEMORY on allocation failure.
 */
[[nodiscard]] CAUDIO_API ca_status ca_mpsc_create(ca_mpsc** out,
                                       size_t capacity,
                                       size_t elem_size,
                                       const cx_allocator* alloc);

/**
 * @brief Destroy a queue.
 *
 * @param queue Queue handle; may be nullptr (no-op).
 */
CAUDIO_API void ca_mpsc_destroy(ca_mpsc* queue);

/**
 * @brief Returns capacity.
 */
[[nodiscard]] CAUDIO_API size_t ca_mpsc_capacity(const ca_mpsc* queue);

/**
 * @brief Returns number of elements currently queued.
 */
[[nodiscard]] CAUDIO_API size_t ca_mpsc_size(const ca_mpsc* queue);

/**
 * @brief Checks if queue is empty.
 */
[[nodiscard]] CAUDIO_API bool ca_mpsc_empty(const ca_mpsc* queue);

/**
 * @brief Pushes a copy of an element into the queue (non-blocking).
 *
 * @param queue Queue handle.
 * @param value Pointer to the element to copy.
 * @return CA_OK on success, CA_OVERFLOW if the queue is full,
 *         CA_INVALID_ARGUMENT if value size doesn't match.
 */
[[nodiscard]] CAUDIO_API ca_status ca_mpsc_push_copy(ca_mpsc* queue,
                                          const void* value);

/**
 * @brief Pops an element from the queue (non-blocking).
 *
 * @param queue Queue handle.
 * @param[out] out Receives the popped element.
 * @return CA_OK on success, CA_INVALID_ARGUMENT if the queue is empty.
 */
[[nodiscard]] CAUDIO_API ca_status ca_mpsc_pop(ca_mpsc* queue, void* out);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_UTILS_MPSC_H */
