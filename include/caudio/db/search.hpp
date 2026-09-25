#pragma once
#include <sqlite3.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <expected>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include <caudio/db/db_core.hpp>
#include <caudio/db/db_types.hpp>
#include <caudio/db/detail.hpp>
#include <caudio/utils.hpp>

namespace caudio::db {

/**
 * @brief Maps a search result row into a Track (alias for fillTrackFromStmt).
 * @ingroup caudio_db
 * @param s Statement positioned on a row.
 * @param out Track to populate.
 * @par Thread safety
 * Caller must hold the DB lock protecting the statement.
 */
inline void fillTrackSearch(sqlite3_stmt* s, Track& out);

/**
 * @brief Executes a single FTS5 MATCH query.
 * @ingroup caudio_db
 * @param h SQLite handle (must be valid, caller holds lock).
 * @param query Already-sanitized FTS5 query text.
 * @param limit Maximum rows (<=0 defaults to 50).
 * @return Vector of matching Tracks, or `Error` on prepare failure.
 * @details SQL: `SELECT ... FROM tracks JOIN tracks_fts ON id=rowid WHERE tracks_fts MATCH ? ORDER
 * BY rank LIMIT ?`.
 * @par Thread safety
 * Caller must hold `Database::mutex()` (shared or exclusive).
 * @see sanitizeFtsTerm
 */
std::expected<std::vector<Track>, caudio::utils::Error>
tryFtsQuery(sqlite3* h, std::string_view query, int limit);

/**
 * @brief FTS5 search with LIKE fallback (three stages).
 * @ingroup caudio_db
 * @param db Database to search (shared lock held for entire operation).
 * @param query Raw user query (sanitized internally).
 * @param limit Maximum rows (<=0 defaults to 50).
 * @return Matching tracks, or `Error` with `StatusCode::Internal` if no DB handle,
 * or `Corrupt`/`Internal` on SQLite errors.
 * @details Stages (all under a single `shared_lock` to avoid unlock gaps):
 * 1) Exact `MATCH sanitized`.
 * 2) Prefix `MATCH sanitized*`.
 * 3) `LIKE %escaped% ESCAPE '\' COLLATE NOCASE` on title/artist/album/album_artist/genre.
 * @par Thread safety
 * Thread-safe: acquires `db.mutex()` as `shared_lock`.
 * @see sanitizeFtsTerm
 * @see tryFtsQuery
 * @see searchLike
 */
std::expected<std::vector<Track>, caudio::utils::Error>
searchFts(Database& db, std::string_view query, int limit = 50);

/**
 * @brief LIKE-only search (no FTS).
 * @ingroup caudio_db
 * @param db Database to search.
 * @param query Raw query substring (escaped for LIKE).
 * @param limit Maximum rows (<=0 defaults to 50).
 * @return Matching tracks, or `Error` with `StatusCode::Internal` if no handle.
 * @details Single `LIKE %escaped%` query across 5 columns with `COLLATE NOCASE`.
 * @par Thread safety
 * Thread-safe: acquires `shared_lock`.
 */
std::expected<std::vector<Track>, caudio::utils::Error>
searchLike(Database& db, std::string_view query, int limit = 50);

/**
 * @brief Public wrapper for `internal::sanitizeFtsTerm`.
 * @ingroup caudio_db
 * @param term Raw user input.
 * @return Sanitized FTS5 term (empty means match-nothing).
 * @details Strips FTS5 operators and escapes quotes by doubling them.
 * @see caudio::db::internal::sanitizeFtsTerm
 */
std::string sanitizeFtsTerm(std::string_view term);

/**
 * @brief Unified search entry point (FTS with fallback).
 * @ingroup caudio_db
 * @param db Database to search.
 * @param query Raw query.
 * @param limit Maximum rows.
 * @return Matching tracks.
 * @par Thread safety
 * Thread-safe (delegates to `searchFts`).
 * @see searchFts
 */
std::expected<std::vector<Track>, caudio::utils::Error> search(Database& db, std::string_view query,
                                                               int limit = 50);

} // namespace caudio::db
