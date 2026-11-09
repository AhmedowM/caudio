#ifndef CAUDIO_MEDIA_FINGERPRINT_H
#define CAUDIO_MEDIA_FINGERPRINT_H

/**
 * @file caudio/media/fingerprint.h
 * @brief Content fingerprinting (sampled vs full).
 * @ingroup caudio_media
 * @details Fingerprints are 32-byte digests used as the primary
 * deduplication key. Mirrors the kanariv::media::internal::computeFingerprint
 * in C.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute a content fingerprint for a file.
 *
 * @param path Filesystem path to hash (UTF-8).
 * @param[out] out Receives the 32-byte digest.
 * @param err Optional error record.
 * @return CA_OK on success, CA_IO_ERROR if the file cannot be sized or
 *         opened, CA_INVALID_ARGUMENT for an empty path.
 *
 * @details Uses BLAKE3-256 (vendored from
 * https://github.com/BLAKE3-team/BLAKE3, v1.8.7, portable-only build).
 * The digest is computed over:
 *   BLAKE3(head[0..64K) || tail[size-64K..size) || LE64(size) || LE32(version=1))
 */
[[nodiscard]] CAUDIO_API ca_status ca_media_fingerprint_compute(const char* path,
                                                     uint8_t out[32],
                                                     ca_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_MEDIA_FINGERPRINT_H */
