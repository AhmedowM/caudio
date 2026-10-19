#include <array>
#include <caudio/utils/error.hpp>
#include <caudio/utils/json.hpp>
#include <caudio/utils/math.hpp>
#include <caudio/utils/record_json.hpp>
#include <caudio/utils/result.hpp>
#include <cstdint>
#include <exception>
#include <expected>
#include <string>
#include <string_view>

namespace caudio::utils {

namespace {

std::array<uint8_t, 32> fallbackFingerprint(std::string_view path) noexcept {
    uint64_t h = 1469598103934665603ULL;
    for (char c : path) {
        h ^= static_cast<uint8_t>(c);
        h *= 1099511628211ULL;
    }
    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 32; i++) {
        out[i] = static_cast<uint8_t>(h >> ((i % 8) * 8));
        h = h * 6364136223846793005ULL + 1;
    }
    return out;
}

} // namespace

Json trackToJson(const caudio::types::Track& t) {
    Json j;
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
    j["fingerprint"] = toHex(t.fingerprint);
    j["path"] = t.path;
    j["title"] = t.title;
    j["artist"] = t.artist;
    j["album"] = t.album;
    j["album_artist"] = t.album_artist;
    j["genre"] = t.genre;
    j["cover_art_path"] = t.cover_art_path;
    return j;
}

std::expected<caudio::types::Track, Error> trackFromJson(const Json& j) {
    try {
        caudio::types::Track t;
        auto getI64 = [&](const char* k, int64_t& out, int64_t def = 0) {
            if (j.contains(k) && !j[k].isNull()) {
                if (j[k].isNumber())
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
            if (j.contains(k) && !j[k].isNull() && j[k].isNumber())
                out = j[k].get<double>();
            else
                out = def;
        };
        auto getStr = [&](const char* k, std::string& out) {
            if (j.contains(k) && !j[k].isNull() && j[k].isString())
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
            if (!fromHex(fpHex, t.fingerprint)) {
                t.fingerprint = fallbackFingerprint(t.path);
            }
        } else {
            t.fingerprint = fallbackFingerprint(t.path);
        }
        return t;
    } catch (const std::exception& e) {
        return std::unexpected{makeError(StatusCode::Corrupt, e.what())};
    }
}

} // namespace caudio::utils
