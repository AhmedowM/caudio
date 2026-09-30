#pragma once

#include <caudio/db/db_types.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/json.hpp>
#include <expected>
#include <filesystem>

// Forward declarations for SQLite handles (sqlite3.h stays in .cpp files).
struct sqlite3;
struct sqlite3_stmt;

namespace caudio::db {
// Defined in db_core.hpp (full Database API); reference params need only this.
class Database;

/**
 * @brief Serializes a Track to JSON.
 * @ingroup caudio_db
 * @param t Track to serialize.
 * @return JSON object with all Track fields; fingerprint is hex-encoded.
 * Keys keep insertion order (ordered backend behind `caudio::utils::Json`).
 * @par Thread safety
 * Pure function, thread-safe.
 */
caudio::utils::Json trackToJson(const Track& t);

/**
 * @brief Deserializes a Track from ordered JSON.
 * @ingroup caudio_db
 * @param j JSON object (as produced by `trackToJson`).
 * @return Track on success, or `Error` with `StatusCode::Corrupt` if parsing fails.
 * @details Missing keys use defaults; `library_id == 0` is normalized to 1.
 * If `fingerprint` is missing or not valid hex, a fallback fingerprint
 * derived from `path` is used (`internal::fallbackFingerprint`).
 * @par Thread safety
 * Pure function, thread-safe.
 * @see trackToJson
 */
std::expected<Track, caudio::utils::Error> trackFromJson(const caudio::utils::Json& j);

/**
 * @brief Exports all tracks to a JSON file.
 * @ingroup caudio_db
 * @param db Database to export from.
 * @param outPath Destination file path.
 * @return Success, or `Error` with `StatusCode::Internal`/`Io` on DB or file failure.
 * @details Calls `db.listTracks(nullptr)` and writes `{"tracks": [...]}` with
 * 2-space indentation. Holds a shared lock via `listTracks`.
 * @par Thread safety
 * Thread-safe (shared lock).
 * @see importJson
 * @see trackToJson
 */
std::expected<void, caudio::utils::Error> exportJson(Database& db,
                                                     const std::filesystem::path& outPath);

/**
 * @brief Imports tracks from a JSON file (transactional upsert).
 * @ingroup caudio_db
 * @param db Database to import into.
 * @param inPath Source file path.
 * @return Success, or `Error` with `StatusCode::Io` if file cannot be opened,
 * `StatusCode::Corrupt` if JSON is invalid or missing `tracks` array, or
 * `StatusCode::Internal` on DB errors. On `Corrupt`, the transaction is rolled back.
 * @details Reads the entire file, parses JSON, validates `tracks` is an array,
 * then inserts each track inside a single `DbTransaction` (`BEGIN IMMEDIATE`).
 * On `SQLITE_CONSTRAINT` (duplicate fingerprint), looks up the existing row
 * and updates it; path collisions are silently ignored. Holds `db.mutex()`
 * exclusively for the transaction duration.
 * @par Thread safety
 * Thread-safe: acquires `db.mutex()` as `unique_lock`.
 * @par Lock ordering
 * `db.mutex()` (dbMutex_) exclusively for the whole import.
 * @see exportJson
 * @see trackFromJson
 */
std::expected<void, caudio::utils::Error> importJson(Database& db,
                                                     const std::filesystem::path& inPath);

} // namespace caudio::db
