module;
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

/**
 * @file search.cppm
 * @brief Full-text search (FTS5 + LIKE fallback).
 * @ingroup caudio_db
 * @details Three-stage search:
 * 1) Exact FTS5 MATCH on `tracks_fts` with sanitized term.
 * 2) Prefix MATCH (`term*`) if exact yields no rows.
 * 3) LIKE fallback on title/artist/album/album_artist/genre with
 *    `ESCAPE '\'` and `COLLATE NOCASE`.
 * FTS5 quoting: `sanitizeFtsTerm()` strips FTS5 operators (`AND`/`OR`/`NOT`/`NEAR`,
 * `*`, `:`, `-`, `(`, `)`, `^`, `~`, `'`) and escapes `"` by doubling it,
 * preventing syntax errors from user input. See `fts.cppm`.
 */

export module caudio.db:search;

import caudio.utils;
import :types;
import :detail;
import :core;

namespace caudio::db {

/**
 * @brief Maps a search result row into a Track (alias for fillTrackFromStmt).
 * @ingroup caudio_db
 * @param s Statement positioned on a row.
 * @param out Track to populate.
 * @par Thread safety
 * Caller must hold the DB lock protecting the statement.
 */
inline void fillTrackSearch(sqlite3_stmt* s, Track& out) {
    internal::fillTrackFromStmt(s, out);
}

/**
 * @brief Executes a single FTS5 MATCH query.
 * @ingroup caudio_db
 * @param h SQLite handle (must be valid, caller holds lock).
 * @param query Already-sanitized FTS5 query text.
 * @param limit Maximum rows (<=0 defaults to 50).
 * @return Vector of matching Tracks, or `Error` on prepare failure.
 * @details SQL: `SELECT ... FROM tracks JOIN tracks_fts ON id=rowid WHERE tracks_fts MATCH ? ORDER BY rank LIMIT ?`.
 * @par Thread safety
 * Caller must hold `Database::mutex()` (shared or exclusive).
 * @see sanitizeFtsTerm
 */
inline std::expected<std::vector<Track>, caudio::utils::Error>
tryFtsQuery(sqlite3* h, std::string_view query, int limit) {
    const char* sql =
        "SELECT t.id, t.fingerprint, t.path, t.deleted_at, t.size, t.mtime, t.duration, "
        "t.sample_rate, t.channels, t.bitrate, t.title, t.artist, t.album, t.album_artist, "
        "t.genre, t.year, t.track_num, t.disc_num, t.cover_art_path, t.rating, t.play_count, "
        "t.last_played, t.date_added, t.last_scanned, t.dirty, t.library_id "
        "FROM tracks t JOIN tracks_fts f ON t.id = f.rowid WHERE tracks_fts MATCH ? ORDER BY rank "
        "LIMIT ?";
    SqliteStatement st;
    if (auto e = st.prepare(h, sql); !e) {
        return std::unexpected{e.error()};
    }
    st.bindText(1, query);
    st.bindInt(2, limit > 0 ? limit : 50);
    std::vector<Track> out;
    while (st.step()) {
        Track t;
        fillTrackSearch(st.get(), t);
        out.push_back(std::move(t));
    }
    return out;
}

// sanitizeFtsTerm: quoted "…" phrase preserved, FTS5 syntax stripped

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
export std::expected<std::vector<Track>, caudio::utils::Error>
searchFts(Database& db, std::string_view query, int limit = 50) {
    if (query.empty())
        return std::vector<Track>{};
    std::string sanitized = internal::sanitizeFtsTerm(query);
    if (sanitized.empty())
        return std::vector<Track>{};
    std::string ftsQ = sanitized;
    // Hold single shared_lock across FTS exact + prefix + LIKE fallback to avoid unlock gap
    std::shared_lock lock(db.mutex());
    sqlite3* h = db.handle();
    if (!h)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
    // try exact FTS query
    auto r = tryFtsQuery(h, ftsQ, limit);
    if (r && !r->empty())
        return r;
    // try prefix
    std::string prefix = sanitized + "*";
    auto r2 = tryFtsQuery(h, prefix, limit);
    if (r2 && !r2->empty())
        return r2;
    // LIKE fallback on 5 cols with COLLATE NOCASE — still under same lock
    std::string esc = internal::escapeLike(sanitized);
    std::string pat = "%" + esc + "%";
    const char* likeSql =
        "SELECT id, fingerprint, path, deleted_at, size, mtime, duration, sample_rate, channels, "
        "bitrate, title, artist, album, album_artist, genre, year, track_num, disc_num, "
        "cover_art_path, rating, play_count, last_played, date_added, last_scanned, dirty, "
        "library_id "
        "FROM tracks WHERE title LIKE ? ESCAPE '\\' COLLATE NOCASE OR artist LIKE ? ESCAPE '\\' "
        "COLLATE NOCASE OR album LIKE ? ESCAPE '\\' COLLATE NOCASE OR album_artist LIKE ? ESCAPE "
        "'\\' COLLATE NOCASE OR genre LIKE ? ESCAPE '\\' COLLATE NOCASE LIMIT ?";
    SqliteStatement st;
    if (auto e = st.prepare(h, likeSql); !e)
        return std::unexpected{e.error()};
    st.bindText(1, pat);
    st.bindText(2, pat);
    st.bindText(3, pat);
    st.bindText(4, pat);
    st.bindText(5, pat);
    st.bindInt(6, limit > 0 ? limit : 50);
    std::vector<Track> out;
    while (st.step()) {
        Track t;
        internal::fillTrackFromStmt(st.get(), t);
        out.push_back(std::move(t));
    }
    return out;
}

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
export std::expected<std::vector<Track>, caudio::utils::Error>
searchLike(Database& db, std::string_view query, int limit = 50) {
    if (query.empty())
        return std::vector<Track>{};
    std::string esc = internal::escapeLike(query);
    std::string pat = "%" + esc + "%";
    std::shared_lock lock(db.mutex());
    sqlite3* h = db.handle();
    if (!h)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
    const char* likeSql =
        "SELECT id, fingerprint, path, deleted_at, size, mtime, duration, sample_rate, channels, "
        "bitrate, title, artist, album, album_artist, genre, year, track_num, disc_num, "
        "cover_art_path, rating, play_count, last_played, date_added, last_scanned, dirty, "
        "library_id "
        "FROM tracks WHERE title LIKE ? ESCAPE '\\' COLLATE NOCASE OR artist LIKE ? ESCAPE '\\' "
        "COLLATE NOCASE OR album LIKE ? ESCAPE '\\' COLLATE NOCASE OR album_artist LIKE ? ESCAPE "
        "'\\' COLLATE NOCASE OR genre LIKE ? ESCAPE '\\' COLLATE NOCASE LIMIT ?";
    SqliteStatement st;
    if (auto e = st.prepare(h, likeSql); !e)
        return std::unexpected{e.error()};
    st.bindText(1, pat);
    st.bindText(2, pat);
    st.bindText(3, pat);
    st.bindText(4, pat);
    st.bindText(5, pat);
    st.bindInt(6, limit > 0 ? limit : 50);
    std::vector<Track> out;
    while (st.step()) {
        Track t;
        internal::fillTrackFromStmt(st.get(), t);
        out.push_back(std::move(t));
    }
    return out;
}

/**
 * @brief Public wrapper for `internal::sanitizeFtsTerm`.
 * @ingroup caudio_db
 * @param term Raw user input.
 * @return Sanitized FTS5 term (empty means match-nothing).
 * @details Strips FTS5 operators and escapes quotes by doubling them.
 * @see caudio::db::internal::sanitizeFtsTerm
 */
export std::string sanitizeFtsTerm(std::string_view term) {
    return internal::sanitizeFtsTerm(term);
}

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
export std::expected<std::vector<Track>, caudio::utils::Error>
search(Database& db, std::string_view query, int limit = 50) {
    return searchFts(db, query, limit);
}

} // namespace caudio::db
