#ifndef CAUDIO_UTILS_RING_H
#define CAUDIO_UTILS_RING_H

/**
 * @file caudio/utils/ring.h
 * @brief Single-producer single-consumer ring buffer.
 * @ingroup caudio_utils
 * @details SPSC ring buffer with cache-line isolation. Exactly one producer
 * may call ca_ring_write() and exactly one consumer may call ca_ring_read().
 * Copying is disabled.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>
#include <cstdx/allocator.h>

#include <stdalign.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Cache-line size used to pad atomics against false sharing. */
#define CA_RING_CACHE_LINE 64U

/**
 * @brief Opaque ring buffer handle.
 */
typedef struct ca_ring ca_ring;

/**
 * @brief Create a ring buffer.
 *
 * @param[out] out Receives the new ring handle on success.
 * @param capacity_frames Number of frames (capacity in frames, not samples).
 * @param channels Number of channels per frame (default 1).
 * @param alloc Allocator for storage, or nullptr for default.
 * @return CA_OK on success, CA_OUT_OF_MEMORY on allocation failure.
 */
[[nodiscard]] CAUDIO_API ca_status ca_ring_create(ca_ring** out,
                                       size_t capacity_frames,
                                       uint32_t channels,
                                       const cx_allocator* alloc);

/**
 * @brief Destroy a ring buffer.
 *
 * @param ring Ring handle; may be nullptr (no-op).
 */
CAUDIO_API void ca_ring_destroy(ca_ring* ring);

/**
 * @brief Returns capacity in frames.
 */
[[nodiscard]] CAUDIO_API size_t ca_ring_capacity(const ca_ring* ring);

/**
 * @brief Returns channel count per frame.
 */
[[nodiscard]] CAUDIO_API uint32_t ca_ring_channels(const ca_ring* ring);

/**
 * @brief Writes frames from a pointer (producer-only).
 *
 * @param ring Ring handle.
 * @param data Pointer to interleaved samples.
 * @param frames Number of frames to write.
 * @return Frames actually written (clamped to free space).
 */
[[nodiscard]] CAUDIO_API size_t ca_ring_write(ca_ring* ring,
                                   const void* data,
                                   size_t frames);

/**
 * @brief Reads frames into a pointer (consumer-only).
 *
 * @param ring Ring handle.
 * @param out Destination pointer for interleaved samples.
 * @param frames Max frames to read.
 * @return Frames actually read (clamped to available).
 */
[[nodiscard]] CAUDIO_API size_t ca_ring_read(ca_ring* ring,
                                  void* out,
                                  size_t frames);

/**
 * @brief Returns frames available for reading.
 */
[[nodiscard]] CAUDIO_API size_t ca_ring_available_read(const ca_ring* ring);

/**
 * @brief Returns frames available for writing.
 */
[[nodiscard]] CAUDIO_API size_t ca_ring_available_write(const ca_ring* ring);

/**
 * @brief Resets read and write counters to zero.
 *
 * @details Requires external synchronization: caller must ensure producer
 * and consumer are stopped before calling.
 */
CAUDIO_API void ca_ring_reset(ca_ring* ring);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_UTILS_RING_H */
