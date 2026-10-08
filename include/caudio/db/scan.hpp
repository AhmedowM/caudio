#pragma once

/**
 * @file scan.hpp
 * @brief Filesystem scan helpers for audio tracks.
 * @ingroup caudio_db
 * @details Implements `scanDirectory` and `scanLibrary` for recursive
 * filesystem traversal, fingerprinting and upsert into the database.
 * Shared with the service layer (playlist/scan imports).
 */

#include <algorithm>
#include <caudio/db/types.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/generator.hpp>
#include <cctype>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace caudio::db {
// Defined in db/core.hpp (full Database API); reference params need only this.
class Database;
} // namespace caudio::db

// Forward declarations for SQLite handles (sqlite3.h stays in .cpp files).
struct sqlite3;
struct sqlite3_stmt;

namespace caudio::db {

/**
 * @brief Scan fingerprinting mode.
 * @ingroup caudio_db
 */
enum class ScanMode {
    Sampled, ///< Sampled BLAKE3 (head+tail+size+version) -- fast, default.
    Full     ///< Full-file BLAKE3 -- slower, more collision-resistant.
};

namespace detail {

/**
 * @brief Checks if a path has a supported audio extension.
 * @ingroup caudio_db
 * @param p Path to check.
 * @return true if extension is .mp3/.flac/.ogg/.wav/.m4a (case-insensitive).
 */
inline bool hasAudioExt(const std::filesystem::path& p) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".mp3" || ext == ".flac" || ext == ".ogg" || ext == ".wav" || ext == ".m4a";
}

} // namespace detail

/**
 * @brief Lazily scans a directory tree yielding Tracks.
 * @ingroup caudio_db
 * @param root Root directory to traverse recursively.
 * @param mode Fingerprinting mode (Sampled vs Full).
 * @return Generator yielding one `Track` per audio file.
 * @details Uses `recursive_directory_iterator` with `skip_permission_denied`.
 * Shared with the service layer (playlist/scan imports).
 * @par Thread safety
 * Not thread-safe with concurrent filesystem mutation; otherwise re-entrant.
 * @see ScanMode
 * @see scanDirectory
 * @see scanLibrary
 */
caudio::utils::Generator<Track> scan(const std::filesystem::path& root,
                                     ScanMode mode = ScanMode::Sampled);

/**
 * @brief Scans a directory and collects all tracks into a vector.
 * @ingroup caudio_db
 * @param root Root directory.
 * @param mode Fingerprinting mode.
 * @return Vector of tracks, or `Error` (currently always succeeds, returns empty on missing root).
 * @par Thread safety
 * Re-entrant; not thread-safe with concurrent filesystem mutation.
 */
std::expected<std::vector<Track>, caudio::utils::Error>
scanDirectory(const std::filesystem::path& root, ScanMode mode = ScanMode::Sampled);

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
 * for `kBatchSize` (500) files, wrapped in `BEGIN IMMEDIATE / COMMIT`, so
 * concurrent `queueList` never sees partial state and a crash leaves DB
 * consistent per batch. Deduplication: early-exit on `path+size+mtime` match;
 * otherwise fingerprint dedup via `findByFingerprintLocked`/`findByPathLocked`.
 * @par Thread safety
 * Thread-safe: internally acquires `db.mutex()` per batch. Caller must not
 * hold `db.mutex()` across the call.
 * @par Lock ordering
 * Acquires `db.mutex()` (dbMutex_) exclusively per batch; inside the batch
 * uses `*Locked` helpers that assume the lock is held and additionally take
 * `cacheMutex_` internally.
 * @see scanDirectory
 * @see Database::findByPathLocked
 * @see Database::findByFingerprintLocked
 */
std::expected<void, caudio::utils::Error>
scanLibrary(Database& db, int64_t libraryId,
            std::function<void(int64_t, int64_t, std::string_view)> progress = {},
            ScanMode mode = ScanMode::Sampled);

} // namespace caudio::db
