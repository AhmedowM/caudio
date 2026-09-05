module;
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <expected>
#include <mutex>
#include <shared_mutex>
#include <sqlite3.h>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

export module caudio.db:search;

import caudio.utils;
import :types;
import :database;

namespace caudio::db {

export std::string sanitizeFtsTerm(std::string_view term) {
    std::string out;
    out.reserve(term.size() * 2);
    for (char c : term) {
        if (c == '"') {
            out += "\"\"";
        } else if (c == '\'' || c == '*' || c == '-' || c == '(' || c == ')' || c == ':' ||
                   c == '\\' || c == '^')
            out.push_back(' ');
        else
            out.push_back(c);
    }
    // remove standalone FTS5 operators OR AND NOT NEAR (case-insensitive)
    std::istringstream iss(out);
    std::string tok;
    std::string rebuilt;
    bool first = true;
    while (iss >> tok) {
        std::string up = tok;
        for (char &ch : up)
            ch = std::toupper((unsigned char)ch);
        if (up == "OR" || up == "AND" || up == "NOT" || up == "NEAR")
            continue;
        if (!first)
            rebuilt.push_back(' ');
        rebuilt += tok;
        first = false;
    }
    // trim
    size_t s = 0;
    while (s < rebuilt.size() && std::isspace((unsigned char)rebuilt[s]))
        s++;
    size_t e = rebuilt.size();
    while (e > s && std::isspace((unsigned char)rebuilt[e - 1]))
        e--;
    return rebuilt.substr(s, e - s);
}

inline std::string escapeLike(std::string_view s) {
    std::string o;
    o.reserve(s.size() * 2);
    for (char c : s) {
        if (c == '%' || c == '_' || c == '\\')
            o.push_back('\\');
        o.push_back(c);
    }
    return o;
}

inline void fillTrackSearch(sqlite3_stmt *s, Track &out) {
    fillTrackFromStmt(s, out);
}

export std::expected<std::vector<Track>, caudio::utils::Error>
searchFts(Database &db, std::string_view query, int limit = 50) {
    if (query.empty())
        return std::vector<Track>{};
    std::string sanitized = sanitizeFtsTerm(query);
    if (sanitized.empty())
        return std::vector<Track>{};
    std::string ftsQ = sanitized;
    const char *sql =
        "SELECT t.id, t.fingerprint, t.path, t.deleted_at, t.size, t.mtime, t.duration, "
        "t.sample_rate, t.channels, t.bitrate, t.title, t.artist, t.album, t.album_artist, "
        "t.genre, t.year, t.track_num, t.disc_num, t.cover_art_path, t.rating, t.play_count, "
        "t.last_played, t.date_added, t.last_scanned, t.dirty, t.library_id "
        "FROM tracks t JOIN tracks_fts f ON t.id = f.rowid WHERE tracks_fts MATCH ? ORDER BY rank "
        "LIMIT ?";
    // try FTS
    {
        std::shared_lock lock(db.mutex());
        sqlite3 *h = db.handle();
        if (!h)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(h, sql); !e) {
            // FTS prepare failed -> fallback to LIKE
        } else {
            st.bindText(1, ftsQ);
            st.bindInt(2, limit > 0 ? limit : 50);
            std::vector<Track> out;
            while (st.step()) {
                Track t;
                fillTrackSearch(st.get(), t);
                out.push_back(std::move(t));
            }
            if (!out.empty())
                return out;
            // try prefix
            std::string prefix = sanitized + "*";
            Statement st2;
            if (auto e2 = st2.prepare(h, sql); e2) {
                st2.bindText(1, prefix);
                st2.bindInt(2, limit > 0 ? limit : 50);
                while (st2.step()) {
                    Track t;
                    fillTrackSearch(st2.get(), t);
                    out.push_back(std::move(t));
                }
                if (!out.empty())
                    return out;
            }
        }
    }
    // LIKE fallback on 5 cols with COLLATE NOCASE
    std::shared_lock lock(db.mutex());
    sqlite3 *h = db.handle();
    if (!h)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    std::string esc = escapeLike(sanitized);
    std::string pat = "%" + esc + "%";
    const char *likeSql =
        "SELECT id, fingerprint, path, deleted_at, size, mtime, duration, sample_rate, channels, "
        "bitrate, title, artist, album, album_artist, genre, year, track_num, disc_num, "
        "cover_art_path, rating, play_count, last_played, date_added, last_scanned, dirty, "
        "library_id "
        "FROM tracks WHERE title LIKE ? ESCAPE '\\' COLLATE NOCASE OR artist LIKE ? ESCAPE '\\' "
        "COLLATE NOCASE OR album LIKE ? ESCAPE '\\' COLLATE NOCASE OR album_artist LIKE ? ESCAPE "
        "'\\' COLLATE NOCASE OR genre LIKE ? ESCAPE '\\' COLLATE NOCASE LIMIT ?";
    Statement st;
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
        fillTrackFromStmt(st.get(), t);
        out.push_back(std::move(t));
    }
    return out;
}

export std::expected<std::vector<Track>, caudio::utils::Error>
searchLike(Database &db, std::string_view term, int limit = 50) {
    if (term.empty())
        return std::vector<Track>{};
    std::string esc = escapeLike(term);
    std::string pat = "%" + esc + "%";
    std::shared_lock lock(db.mutex());
    sqlite3 *h = db.handle();
    if (!h)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    const char *likeSql =
        "SELECT id, fingerprint, path, deleted_at, size, mtime, duration, sample_rate, channels, "
        "bitrate, title, artist, album, album_artist, genre, year, track_num, disc_num, "
        "cover_art_path, rating, play_count, last_played, date_added, last_scanned, dirty, "
        "library_id "
        "FROM tracks WHERE title LIKE ? ESCAPE '\\' COLLATE NOCASE OR artist LIKE ? ESCAPE '\\' "
        "COLLATE NOCASE OR album LIKE ? ESCAPE '\\' COLLATE NOCASE OR album_artist LIKE ? ESCAPE "
        "'\\' COLLATE NOCASE OR genre LIKE ? ESCAPE '\\' COLLATE NOCASE LIMIT ?";
    Statement st;
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
        fillTrackFromStmt(st.get(), t);
        out.push_back(std::move(t));
    }
    return out;
}

// Unified search entry
export std::expected<std::vector<Track>, caudio::utils::Error>
search(Database &db, std::string_view query, int limit = 50) {
    return searchFts(db, query, limit);
}

} // namespace caudio::db
