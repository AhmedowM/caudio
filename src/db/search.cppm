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

export module caudio.db:search;

import caudio.utils;
import :types;
import :detail;
import :core;

namespace caudio::db {

inline void fillTrackSearch(sqlite3_stmt* s, Track& out) {
    internal::fillTrackFromStmt(s, out);
}

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

export std::string sanitizeFtsTerm(std::string_view term) {
    return internal::sanitizeFtsTerm(term);
}

export std::expected<std::vector<Track>, caudio::utils::Error>
search(Database& db, std::string_view query, int limit = 50) {
    return searchFts(db, query, limit);
}

} // namespace caudio::db
