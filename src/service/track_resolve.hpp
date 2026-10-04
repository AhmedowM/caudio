/**
 * @file track_resolve.hpp
 * @brief Shared single-file library ensure helper (service-local, header-only).
 */
#pragma once

#include <caudio/db/db_core.hpp>
#include <caudio/db/db_types.hpp>
#include <caudio/player/decoder.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/result.hpp>
#include <db/fingerprint.hpp>
#include <expected>
#include <filesystem>
#include <utility>

#include "service_audio.hpp"

namespace caudio::service::detail {

/**
 * @brief Fingerprints a file, extracts metadata, and inserts (or reuses) its
 * library row.
 * @param db Database.
 * @param p Existing audio file path.
 * @return Track row (with id) or Error.
 */
inline std::expected<caudio::db::Track, caudio::utils::Error>
ensureLibraryTrack(caudio::db::Database& db, const std::filesystem::path& p) {
    auto fpRes = caudio::db::internal::computeFingerprint(p);
    if (!fpRes)
        return std::unexpected{fpRes.error()};
    caudio::db::Track t;
    t.path = p.generic_string();
    t.fingerprint = *fpRes;
    t.duration = durationFromDecoder(p);
    std::error_code ec2;
    auto sz = std::filesystem::file_size(p, ec2);
    if (!ec2)
        t.size = static_cast<int64_t>(sz);
    auto ftime = std::filesystem::last_write_time(p, ec2);
    if (!ec2)
        t.mtime = static_cast<int64_t>(ftime.time_since_epoch().count());
    // Extract metadata (title, artist, album, etc.) like library add.
    if (auto meta = caudio::player::extractMetadata(t.path); meta) {
        t.title = std::move(meta->title);
        t.artist = std::move(meta->artist);
        t.album = std::move(meta->album);
        t.album_artist = std::move(meta->album_artist);
        t.genre = std::move(meta->genre);
        t.year = meta->year;
        t.track_num = meta->track_num;
        t.disc_num = meta->disc_num;
        if (meta->duration > 0)
            t.duration = meta->duration;
        t.sample_rate = static_cast<uint32_t>(meta->sample_rate);
        t.channels = static_cast<uint32_t>(meta->channels);
        t.bitrate = meta->bitrate;
    }
    auto ins = db.insertTrack(t);
    if (ins) {
        t.id = *ins;
        return t;
    }
    if (ins.error().code == caudio::utils::StatusCode::AlreadyExists) {
        if (auto existing = db.findByFingerprint(t.fingerprint))
            return std::move(*existing);
        if (auto byPath = db.findByPath(t.path))
            return std::move(*byPath);
    }
    return std::unexpected{ins.error()};
}

} // namespace caudio::service::detail
