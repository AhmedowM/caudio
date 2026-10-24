#pragma once

/**
 * @file scan.hpp
 * @brief Database-side scan helpers (persistence half of library scanning).
 * @ingroup caudio_db
 * @details Library scanning is split at the persistence seam:
 * - `caudio::media` owns the pure, DB-free half: filesystem walk,
 *   extension filter, BLAKE3 fingerprinting and metadata extraction
 *   (`caudio::media::scanTracks`, `caudio::media::scanTracksToVector`).
 * - `caudio::db` keeps the DB-coupled half: `scanLibrary` (batched upsert
 *   of the discovered tracks under the database mutex).
 *
 * The `caudio::db::scan`, `caudio::db::scanDirectory` and
 * `caudio::db::ScanMode` spellings remain as compatibility forwarders to the
 * media spellings and are deprecated (documentation only -- the module
 * partition re-exports them, so no `[[deprecated]]` attribute is used).
 */

#include <caudio/db/types.hpp>
#include <caudio/fwd.hpp>
#include <caudio/media/audio_ext.hpp>
#include <caudio/media/scan.hpp>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace caudio::db {

/**
 * @brief Compatibility alias of `caudio::media::ScanMode`.
 * @ingroup caudio_db
 * @details Scan fingerprinting mode (Sampled vs Full).
 * @deprecated Use `caudio::media::ScanMode` instead.
 */
using ScanMode = caudio::media::ScanMode;

namespace detail {

/**
 * @brief Compatibility forwarder to `caudio::media::hasAudioExt`.
 * @ingroup caudio_db
 * @param p Path to check.
 * @return true if extension is a supported audio extension.
 * @deprecated Use `caudio::media::hasAudioExt` instead.
 */
[[deprecated("use caudio::media::hasAudioExt")]]
inline bool hasAudioExt(const std::filesystem::path& p) {
    return caudio::media::hasAudioExt(p);
}

} // namespace detail

/**
 * @brief Compatibility forwarder to `caudio::media::scanTracks`.
 * @ingroup caudio_db
 * @param root Root directory to traverse recursively.
 * @param mode Fingerprinting mode (Sampled vs Full).
 * @return Generator yielding one `Track` per audio file.
 * @deprecated Use `caudio::media::scanTracks` instead.
 */
inline caudio::utils::Generator<caudio::types::Track> scan(const std::filesystem::path& root,
                                                           ScanMode mode = ScanMode::Sampled) {
    return caudio::media::scanTracks(root, mode);
}

/**
 * @brief Compatibility forwarder to `caudio::media::scanTracksToVector`.
 * @ingroup caudio_db
 * @param root Root directory.
 * @param mode Fingerprinting mode.
 * @return Vector of tracks, or `Error` (currently always succeeds, returns empty on missing root).
 * @deprecated Use `caudio::media::scanTracksToVector` instead.
 */
inline std::expected<std::vector<caudio::types::Track>, caudio::utils::Error>
scanDirectory(const std::filesystem::path& root, ScanMode mode = ScanMode::Sampled) {
    return caudio::media::scanTracksToVector(root, mode);
}

/**
 * @brief Scans a library root and upserts tracks into the database.
 * @ingroup caudio_db
 * @param db Database to update.
 * @param libraryId Library id whose `path` is the scan root.
 * @param progress Optional callback `progress(scanned, total, path)` invoked per file.
 * @return Success or `Error` with `StatusCode::InvalidArg` if libraryId==0,
 * `StatusCode::NotFound` if library or path not found, `StatusCode::Busy` if
 * `BEGIN IMMEDIATE` fails, or `StatusCode::Internal` on commit failure.
 * @details Batching: holds `Database::mutex()` as `unique_lock<shared_mutex>`
 * for `batchSize` (500) files, wrapped in `BEGIN IMMEDIATE / COMMIT`, so
 * concurrent `listQueueItems` never sees partial state and a crash leaves DB
 * consistent per batch. Deduplication: early-exit on `path+size+mtime` match;
 * otherwise fingerprint dedup via `findByFingerprintLocked`/`findByPathLocked`.
 * Discovery (walk, fingerprint, tags) is done by `caudio::media::scanTracks`.
 * @par Thread safety
 * Thread-safe: internally acquires `db.mutex()` per batch. Caller must not
 * hold `db.mutex()` across the call.
 * @par Lock ordering
 * Acquires `db.mutex()` (dbMutex_) exclusively per batch; inside the batch
 * uses `*Locked` helpers that assume the lock is held and additionally take
 * `cacheMutex_` internally.
 * @see caudio::media::scanTracks
 * @see Database::findByPathLocked
 * @see Database::findByFingerprintLocked
 */
std::expected<void, caudio::utils::Error>
scanLibrary(Database& db, int64_t libraryId,
            std::function<void(int64_t, int64_t, std::string_view)> progress = {},
            ScanMode mode = ScanMode::Sampled);

} // namespace caudio::db
