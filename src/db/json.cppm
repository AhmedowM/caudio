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

export module caudio.db:json;

import caudio.utils;
import :types;
import :detail;
import :database;
import :statement;
import :transaction;

namespace caudio::db {

export using ordered_json = nlohmann::ordered_json;

// DRY: canonical hex helpers live in caudio.db:detail — thin wrappers for backwards compat
inline std::string fingerprintToHex(const std::array<uint8_t, 32>& fp) {
    return detail::toHex(fp);
}
inline bool hexToFingerprint(std::string_view hex, std::array<uint8_t, 32>& out) {
    return detail::fromHex(hex, out);
}

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
    j["album_artist"] = t.albumArtist;
    j["genre"] = t.genre;
    j["cover_art_path"] = t.cover_art_path;
    return j;
}

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
        getStr("album_artist", t.albumArtist);
        getStr("genre", t.genre);
        getStr("cover_art_path", t.cover_art_path);
        std::string fpHex;
        getStr("fingerprint", fpHex);
        if (!fpHex.empty()) {
            if (!hexToFingerprint(fpHex, t.fingerprint)) {
                t.fingerprint = detail::fallbackFingerprint(t.path);
            }
        } else {
            t.fingerprint = detail::fallbackFingerprint(t.path);
        }
        return t;
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, e.what())};
    }
}

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
            caudio::utils::makeError(caudio::utils::Result::Io, "cannot open output")};
    f << root.dump(2);
    if (!f)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "write failed")};
    return {};
}

export std::expected<void, caudio::utils::Error> importJson(Database& db,
                                                            const std::filesystem::path& inPath) {
    std::ifstream f(inPath, std::ios::binary);
    if (!f)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::Result::Io, "cannot open input")};
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (content.empty())
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::Result::Corrupt, "empty file")};
    ordered_json root;
    try {
        root = ordered_json::parse(content);
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, e.what())};
    }
    if (!root.contains("tracks") || !root["tracks"].is_array()) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::Result::Corrupt, "missing tracks array")};
    }
    std::unique_lock lk{db.mutex()};
    sqlite3* h = db.handleLocked();
    if (!h)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    auto txRes = Transaction::begin(h);
    if (!txRes)
        return std::unexpected{txRes.error()};
    Transaction tx = std::move(*txRes);
    bool corrupt = false;
    for (auto& j : root["tracks"]) {
        auto tr = trackFromJson(j);
        if (!tr) {
            corrupt = true;
            break;
        }
        Track t = *tr;
        Statement stmt;
        if (auto e = stmt.prepare(h,
                                   "INSERT INTO tracks (fingerprint, path, size, mtime, duration, "
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
        stmt.bindText(12, t.albumArtist);
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
            Statement sel;
            if (auto e = sel.prepare(h, "SELECT id FROM tracks WHERE fingerprint=?"); !e) {
                corrupt = true;
                break;
            }
            sel.bindBlob(1, fpSpan);
            if (sel.step()) {
                int64_t existing = sel.columnInt(0);
                Statement upd;
                if (auto e = upd.prepare(h,
                                          "UPDATE tracks SET path=?, size=?, mtime=?, duration=?, "
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
                upd.bindText(11, t.albumArtist);
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
                Statement sel2;
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
            caudio::utils::makeError(caudio::utils::Result::Corrupt, "import corrupt")};
    }
    if (auto c = tx.commit(); !c)
        return std::unexpected{c.error()};
    return {};
}

} // namespace caudio::db
