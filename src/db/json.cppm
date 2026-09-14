module;
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

/**
 * @file json.cppm
 * @brief JSON import/export for tracks.
 * @ingroup caudio_db
 * @details Serializes `Track` to/from `nlohmann::ordered_json` and
 * provides `exportJson`/`importJson` that operate on the database.
 * Fingerprints are hex-encoded via `fingerprintToHex`/`hexToFingerprint`
 * (wrappers over `internal::toHex`/`fromHex`). Import is transactional
 * and upserts on fingerprint collision.
 */

export module caudio.db:json;

import caudio.utils;
import :types;
import :detail;
import :core;
import :SqliteStatement;
import :DbTransaction;

namespace caudio::db {

/**
 * @brief Ordered JSON type used for track serialization.
 * @ingroup caudio_db
 */
export using ordered_json = nlohmann::ordered_json;

// DRY: canonical hex helpers live in caudio.db:detail — thin wrappers for backwards compat
/**
 * @brief Converts a 32-byte fingerprint to a 64-char lowercase hex string.
 * @ingroup caudio_db
 * @param fp Fingerprint bytes.
 * @return Hex string (64 chars).
 * @see hexToFingerprint
 */
inline std::string fingerprintToHex(const std::array<uint8_t, 32>& fp) {
    return internal::toHex(fp);
}
/**
 * @brief Parses a 64-char hex string into a fingerprint.
 * @ingroup caudio_db
 * @param hex Hex view (must be 64 chars, case-insensitive).
 * @param out Output bytes.
 * @return true on success, false if length or characters invalid.
 * @see fingerprintToHex
 */
inline bool hexToFingerprint(std::string_view hex, std::array<uint8_t, 32>& out) {
    return internal::fromHex(hex, out);
}

/**
 * @brief Serializes a Track to ordered JSON.
 * @ingroup caudio_db
 * @param t Track to serialize.
 * @return JSON object with all Track fields; fingerprint is hex-encoded.
 * @par Thread safety
 * Pure function, thread-safe.
 */
export ordered_json trackToJson(const Track& t) {
    ordered_json j;
    j["id"] = t.id;
    j["size"] = t.size;
    j["mtime"] = t.mtime;
    j["duration"] = t.duration;
    j["sample_rate"] = t.sample_rate;
    j["channels"] = t.channels;
    j["bitrate"] = t.bitrate;
    j["year"] = t.year;
    j["track_num"] = t.track_num;
    j["disc_num"] = t.disc_num;
    j["rating"] = t.rating;
    j["play_count"] = t.play_count;
    j["last_played"] = t.last_played;
    j["date_added"] = t.date_added;
    j["last_scanned"] = t.last_scanned;
    j["dirty"] = t.dirty;
    j["library_id"] = t.library_id;
    j["deleted_at"] = t.deleted_at;
    j["fingerprint"] = fingerprintToHex(t.fingerprint);
    j["path"] = t.path;
    j["title"] = t.title;
    j["artist"] = t.artist;
    j["album"] = t.album;
    j["album_artist"] = t.album_artist;
    j["genre"] = t.genre;
    j["cover_art_path"] = t.cover_art_path;
    return j;
}

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
export std::expected<Track, caudio::utils::Error> trackFromJson(const ordered_json& j) {
    try {
        Track t;
        auto getI64 = [&](const char* k, int64_t& out, int64_t def = 0) {
            if (j.contains(k) && !j[k].is_null()) {
                if (j[k].is_number())
                    out = j[k].get<int64_t>();
                else
                    out = def;
            } else
                out = def;
        };
        auto getInt = [&](const char* k, int& out, int def = 0) {
            int64_t v = def;
            getI64(k, v, def);
            out = (int)v;
        };
        auto getDbl = [&](const char* k, double& out, double def = 0) {
            if (j.contains(k) && !j[k].is_null() && j[k].is_number())
                out = j[k].get<double>();
            else
                out = def;
        };
        auto getStr = [&](const char* k, std::string& out) {
            if (j.contains(k) && !j[k].is_null() && j[k].is_string())
                out = j[k].get<std::string>();
            else
                out.clear();
        };
        getI64("id", t.id, 0);
        getI64("size", t.size, 0);
        getI64("mtime", t.mtime, 0);
        getDbl("duration", t.duration, 0);
        {
            int64_t v;
            getI64("sample_rate", v, 0);
            t.sample_rate = (uint32_t)v;
        }
        {
            int64_t v;
            getI64("channels", v, 0);
            t.channels = (uint32_t)v;
        }
        {
            int64_t v;
            getI64("bitrate", v, 0);
            t.bitrate = (int32_t)v;
        }
        getInt("year", t.year, 0);
        getInt("track_num", t.track_num, 0);
        getInt("disc_num", t.disc_num, 0);
        getInt("rating", t.rating, 0);
        getI64("play_count", t.play_count, 0);
        getI64("last_played", t.last_played, 0);
        getI64("date_added", t.date_added, 0);
        getI64("last_scanned", t.last_scanned, 0);
        {
            int dirtyTmp = 0;
            getInt("dirty", dirtyTmp, 0);
            t.dirty = dirtyTmp != 0;
        }
        getI64("library_id", t.library_id, 1);
        if (t.library_id == 0)
            t.library_id = 1;
        getI64("deleted_at", t.deleted_at, 0);
        getStr("path", t.path);
        getStr("title", t.title);
        getStr("artist", t.artist);
        getStr("album", t.album);
        getStr("album_artist", t.album_artist);
        getStr("genre", t.genre);
        getStr("cover_art_path", t.cover_art_path);
        std::string fpHex;
        getStr("fingerprint", fpHex);
        if (!fpHex.empty()) {
            if (!hexToFingerprint(fpHex, t.fingerprint)) {
                t.fingerprint = internal::fallbackFingerprint(t.path);
            }
        } else {
            t.fingerprint = internal::fallbackFingerprint(t.path);
        }
        return t;
    } catch (const std::exception& e) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Corrupt, e.what())};
    }
}

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
export std::expected<void, caudio::utils::Error> exportJson(Database& db,
                                                            const std::filesystem::path& outPath) {
    auto tracks = db.listTracks(nullptr);
    if (!tracks)
        return std::unexpected{tracks.error()};
    ordered_json root;
    root["tracks"] = ordered_json::array();
    for (auto& t : *tracks) {
        root["tracks"].push_back(trackToJson(t));
    }
    std::ofstream f(outPath, std::ios::binary);
    if (!f)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io, "cannot open output")};
    f << root.dump(2);
    if (!f)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io, "write failed")};
    return {};
}

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
export std::expected<void, caudio::utils::Error> importJson(Database& db,
                                                            const std::filesystem::path& inPath) {
    std::ifstream f(inPath, std::ios::binary);
    if (!f)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io, "cannot open input")};
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (content.empty())
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Corrupt, "empty file")};
    ordered_json root;
    try {
        root = ordered_json::parse(content);
    } catch (const std::exception& e) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Corrupt, e.what())};
    }
    if (!root.contains("tracks") || !root["tracks"].is_array()) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Corrupt, "missing tracks array")};
    }
    std::unique_lock lk{db.mutex()};
    sqlite3* h = db.handleLocked();
    if (!h)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
    auto txRes = DbTransaction::begin(h);
    if (!txRes)
        return std::unexpected{txRes.error()};
    DbTransaction tx = std::move(*txRes);
    bool corrupt = false;
    for (auto& j : root["tracks"]) {
        auto tr = trackFromJson(j);
        if (!tr) {
            corrupt = true;
            break;
        }
        Track t = *tr;
        SqliteStatement stmt;
        if (auto e =
                stmt.prepare(h, "INSERT INTO tracks (fingerprint, path, size, mtime, duration, "
                                "sample_rate, channels, bitrate, title, artist, album, "
                                "album_artist, genre, year, track_num, disc_num, "
                                "cover_art_path, rating, play_count, last_played, date_added, "
                                "last_scanned, dirty, library_id, deleted_at) VALUES "
                                "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
            !e) {
            corrupt = true;
            break;
        }
        auto fpSpan = std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(t.fingerprint.data()), t.fingerprint.size()};
        stmt.bindBlob(1, fpSpan);
        stmt.bindText(2, t.path);
        stmt.bindInt(3, t.size);
        stmt.bindInt(4, t.mtime);
        stmt.bindDouble(5, t.duration);
        stmt.bindInt(6, t.sample_rate);
        stmt.bindInt(7, t.channels);
        stmt.bindInt(8, t.bitrate);
        stmt.bindText(9, t.title);
        stmt.bindText(10, t.artist);
        stmt.bindText(11, t.album);
        stmt.bindText(12, t.album_artist);
        stmt.bindText(13, t.genre);
        stmt.bindInt(14, t.year);
        stmt.bindInt(15, t.track_num);
        stmt.bindInt(16, t.disc_num);
        stmt.bindText(17, t.cover_art_path);
        stmt.bindInt(18, t.rating);
        stmt.bindInt(19, t.play_count);
        stmt.bindInt(20, t.last_played);
        if (t.date_added)
            stmt.bindInt(21, t.date_added);
        else
            stmt.bindNull(21);
        stmt.bindInt(22, t.last_scanned);
        stmt.bindInt(23, t.dirty ? 1 : 0);
        stmt.bindInt(24, t.library_id ? t.library_id : 1);
        if (t.deleted_at)
            stmt.bindInt(25, t.deleted_at);
        else
            stmt.bindNull(25);
        int rc = stmt.stepDone();
        if (rc == SQLITE_CONSTRAINT) {
            SqliteStatement sel;
            if (auto e = sel.prepare(h, "SELECT id FROM tracks WHERE fingerprint=?"); !e) {
                corrupt = true;
                break;
            }
            sel.bindBlob(1, fpSpan);
            if (sel.step()) {
                int64_t existing = sel.columnInt(0);
                SqliteStatement upd;
                if (auto e =
                        upd.prepare(h, "UPDATE tracks SET path=?, size=?, mtime=?, duration=?, "
                                       "sample_rate=?, channels=?, bitrate=?, title=?, artist=?, "
                                       "album=?, album_artist=?, genre=?, year=?, track_num=?, "
                                       "disc_num=?, cover_art_path=?, rating=?, play_count=?, "
                                       "last_played=?, date_added=?, last_scanned=?, dirty=?, "
                                       "library_id=?, deleted_at=? WHERE id=?");
                    !e) {
                    corrupt = true;
                    break;
                }
                upd.bindText(1, t.path);
                upd.bindInt(2, t.size);
                upd.bindInt(3, t.mtime);
                upd.bindDouble(4, t.duration);
                upd.bindInt(5, t.sample_rate);
                upd.bindInt(6, t.channels);
                upd.bindInt(7, t.bitrate);
                upd.bindText(8, t.title);
                upd.bindText(9, t.artist);
                upd.bindText(10, t.album);
                upd.bindText(11, t.album_artist);
                upd.bindText(12, t.genre);
                upd.bindInt(13, t.year);
                upd.bindInt(14, t.track_num);
                upd.bindInt(15, t.disc_num);
                upd.bindText(16, t.cover_art_path);
                upd.bindInt(17, t.rating);
                upd.bindInt(18, t.play_count);
                upd.bindInt(19, t.last_played);
                upd.bindInt(20, t.date_added);
                upd.bindInt(21, t.last_scanned);
                upd.bindInt(22, t.dirty ? 1 : 0);
                upd.bindInt(23, t.library_id ? t.library_id : 1);
                if (t.deleted_at)
                    upd.bindInt(24, t.deleted_at);
                else
                    upd.bindNull(24);
                upd.bindInt(25, existing);
                (void)upd.stepDone();
            } else {
                SqliteStatement sel2;
                if (auto e = sel2.prepare(h, "SELECT id FROM tracks WHERE path=?"); e) {
                    sel2.bindText(1, t.path);
                    if (sel2.step()) {
                        // path exists but fingerprint differs — keep existing row, no op for test
                    }
                }
            }
        } else if (rc != SQLITE_DONE) {
            corrupt = true;
            break;
        }
    }
    if (corrupt) {
        (void)tx.rollback();
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Corrupt, "import corrupt")};
    }
    if (auto c = tx.commit(); !c)
        return std::unexpected{c.error()};
    return {};
}

} // namespace caudio::db
