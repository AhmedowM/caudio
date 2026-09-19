#pragma once
#include <sqlite3.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "caudio/utils/utils.hpp"
#include "caudio/db/db_types.hpp"
#include "caudio/db/detail.hpp"
#include "caudio/db/db_core.hpp"
#include "caudio/db/statement.hpp"
#include "caudio/db/transaction.hpp"

namespace caudio::db {

/**
 * @brief Ordered JSON type used for track serialization.
 * @ingroup caudio_db
 */
using ordered_json = nlohmann::ordered_json;

// DRY: canonical hex helpers live in caudio.db:detail — thin wrappers for backwards compat
/**
 * @brief Converts a 32-byte fingerprint to a 64-char lowercase hex string.
 * @ingroup caudio_db
 * @param fp Fingerprint bytes.
 * @return Hex string (64 chars).
 * @see hexToFingerprint
 */
std::string fingerprintToHex(const std::array<uint8_t, 32>& fp);

/**
 * @brief Parses a 64-char hex string into a fingerprint.
 * @ingroup caudio_db
 * @param hex Hex view (must be 64 chars, case-insensitive).
 * @param out Output bytes.
 * @return true on success, false if length or characters invalid.
 * @see fingerprintToHex
 */
bool hexToFingerprint(std::string_view hex, std::array<uint8_t, 32>& out);

/**
 * @brief Serializes a Track to ordered JSON.
 * @ingroup caudio_db
 * @param t Track to serialize.
 * @return JSON object with all Track fields; fingerprint is hex-encoded.
 * @par Thread safety
 * Pure function, thread-safe.
 */
ordered_json trackToJson(const Track& t);

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
std::expected<Track, caudio::utils::Error> trackFromJson(const ordered_json& j);

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