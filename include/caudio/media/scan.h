#ifndef CAUDIO_MEDIA_SCAN_H
#define CAUDIO_MEDIA_SCAN_H

/**
 * @file caudio/media/scan.h
 * @brief Filesystem scan helpers for audio tracks.
 * @ingroup caudio_media
 * @details Library scanning produces ca_track records from a directory tree.
 * Mirrors the kanariv::media::scan interface in C using a callback pattern
 * instead of C++ coroutines.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#include <caudio/types/record.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Scan fingerprinting mode.
 */
typedef enum ca_media_scan_mode {
    CA_MEDIA_SCAN_SAMPLED, /**< Sampled fingerprint (head+tail+size+version) -- fast. */
    CA_MEDIA_SCAN_FULL     /**< Full-file fingerprint -- slower, more collision-resistant. */
} ca_media_scan_mode_t;

/**
 * @brief Callback invoked for each discovered track during scanning.
 *
 * @param track The discovered track (caller does not own it; valid during
 *              the callback only).
 * @param user_data User-provided context pointer.
 * @return false to stop scanning early, true to continue.
 */
typedef bool (*ca_media_track_cb)(const ca_track* track, void* user_data);

/**
 * @brief Lazily scan a directory tree, invoking cb for each audio file.
 *
 * @param root Root directory to traverse recursively (UTF-8).
 * @param mode Fingerprinting mode.
 * @param cb Callback invoked for each track.
 * @param user_data User-provided context pointer.
 * @param err Optional error record.
 * @return CA_OK on success, ca_status error on failure.
 * @details Uses recursive directory iteration, skipping permission-denied
 * entries. Not thread-safe with concurrent filesystem mutation.
 */
[[nodiscard]] CAUDIO_API ca_status ca_media_scan_tracks(const char* root,
                                             ca_media_scan_mode_t mode,
                                             ca_media_track_cb cb,
                                             void* user_data,
                                             ca_error* err);

/**
 * @brief Scan a directory and collect all tracks into a vector.
 *
 * @param root Root directory (UTF-8).
 * @param mode Fingerprinting mode.
 * @param[out] out Receives a newly allocated cx_vector of ca_track values.
 * @param alloc Allocator for the vector and track strings.
 * @param err Optional error record.
 * @return CA_OK on success, ca_status error on failure.
 * @details Returns an empty vector on missing root. The caller owns the
 * vector and must destroy it with cx_vector_destroy (which will destroy
 * each track's strings).
 */
[[nodiscard]] CAUDIO_API ca_status ca_media_scan_tracks_to_vector(const char* root,
                                                        ca_media_scan_mode_t mode,
                                                        cx_vector** out,
                                                        const cx_allocator* alloc,
                                                        ca_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_MEDIA_SCAN_H */
