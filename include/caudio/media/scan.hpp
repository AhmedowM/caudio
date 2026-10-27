#pragma once

/**
 * @file scan.hpp
 * @brief Filesystem scan helpers for audio tracks (discovery, fingerprint, tags).
 * @ingroup caudio_media
 * @details Library scanning is split at the persistence seam:
 * - `caudio::media` owns the pure, DB-free half: filesystem walk,
 *   extension filter, BLAKE3 fingerprinting and metadata extraction,
 *   producing `caudio::types::Track` records.
 * - `caudio::db` keeps the DB-coupled half: `caudio::db::scanLibrary`
 *   (batched upsert of the discovered tracks under the database mutex).
 */

#include <caudio/types/record.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/generator.hpp>
#include <expected>
#include <filesystem>
#include <vector>

namespace caudio::media {

/**
 * @brief Scan fingerprinting mode.
 * @ingroup caudio_media
 */
enum class ScanMode {
    Sampled, ///< Sampled BLAKE3 (head+tail+size+version) -- fast, default.
    Full     ///< Full-file BLAKE3 -- slower, more collision-resistant.
};

/**
 * @brief Lazily scans a directory tree yielding Tracks.
 * @ingroup caudio_media
 * @param root Root directory to traverse recursively.
 * @param mode Fingerprinting mode (Sampled vs Full).
 * @return Generator yielding one `Track` per audio file.
 * @details Uses `recursive_directory_iterator` with `skip_permission_denied`.
 * @par Thread safety
 * Not thread-safe with concurrent filesystem mutation; otherwise re-entrant.
 * @see ScanMode
 * @see scanTracksToVector
 */
CAUDIO_API caudio::utils::Generator<caudio::types::Track> scanTracks(const std::filesystem::path& root,
                                                          ScanMode mode = ScanMode::Sampled);

/**
 * @brief Scans a directory and collects all tracks into a vector.
 * @ingroup caudio_media
 * @param root Root directory.
 * @param mode Fingerprinting mode.
 * @return Vector of tracks, or `Error` (currently always succeeds, returns empty on missing root).
 * @par Thread safety
 * Re-entrant; not thread-safe with concurrent filesystem mutation.
 * @see scanTracks
 */
CAUDIO_API std::expected<std::vector<caudio::types::Track>, caudio::utils::Error>
scanTracksToVector(const std::filesystem::path& root, ScanMode mode = ScanMode::Sampled);

} // namespace caudio::media
