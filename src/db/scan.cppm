module;
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <generator>
#include <string>
#include <system_error>
#include <vector>

#include "blake3.h"

export module caudio.db:scan;

import caudio.utils;
import :types;
import :database;

namespace caudio::db {

export enum class ScanMode { Sampled, Full };

inline bool hasAudioExt(const std::filesystem::path& p) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".mp3" || ext == ".flac" || ext == ".ogg" || ext == ".wav" || ext == ".m4a";
}

export constexpr size_t kSample = 64 * 1024;

export std::expected<std::array<uint8_t, 32>, caudio::utils::Error>
computeFingerprint(const std::filesystem::path& path) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    if (ec)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, ec.message())};
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::Result::Io, "cannot open file")};
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    std::vector<uint8_t> buf(kSample);
    // head
    f.read(reinterpret_cast<char*>(buf.data()), kSample);
    size_t n = (size_t)f.gcount();
    if (n)
        blake3_hasher_update(&hasher, buf.data(), n);
    // tail if file larger than kSample
    if (sz > kSample) {
        f.clear();
        f.seekg((std::streamoff)(sz - kSample), std::ios::beg);
        if (f) {
            f.read(reinterpret_cast<char*>(buf.data()), kSample);
            n = (size_t)f.gcount();
            if (n)
                blake3_hasher_update(&hasher, buf.data(), n);
        }
    }
    uint64_t sz64 = (uint64_t)sz;
    blake3_hasher_update(&hasher, &sz64, sizeof(sz64));
    uint32_t ver = 1;
    blake3_hasher_update(&hasher, &ver, sizeof(ver));
    std::array<uint8_t, 32> out{};
    blake3_hasher_finalize(&hasher, out.data(), out.size());
    return out;
}

// Generator-based scan: yields Tracks lazily
export std::generator<Track> scan(const std::filesystem::path& root,
                                   ScanMode mode = ScanMode::Sampled) {
    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
        co_return;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file(ec) && hasAudioExt(it->path())) {
            Track t;
            t.path = it->path().string();
            std::error_code e2;
            auto sz = it->file_size(e2);
            if (!e2)
                t.size = (int64_t)sz;
            auto ftime = it->last_write_time(e2);
            if (!e2)
                t.mtime = (int64_t)ftime.time_since_epoch().count();
            if (mode == ScanMode::Sampled) {
                auto fp = computeFingerprint(it->path());
                if (fp)
                    t.fingerprint = *fp;
            } else {
                // full file hash via BLAKE3
                std::ifstream f(it->path(), std::ios::binary);
                if (f) {
                    blake3_hasher hasher;
                    blake3_hasher_init(&hasher);
                    char buf[8192];
                    while (f.read(buf, sizeof(buf)) || f.gcount())
                        blake3_hasher_update(&hasher, buf, (size_t)f.gcount());
                    blake3_hasher_finalize(&hasher, t.fingerprint.data(), t.fingerprint.size());
                }
            }
            co_yield t;
        }
    }
}

export std::expected<std::vector<Track>, caudio::utils::Error>
scanDirectory(const std::filesystem::path& root, ScanMode mode = ScanMode::Sampled) {
    std::vector<Track> out;
    for (auto t : scan(root, mode))
        out.push_back(std::move(t));
    return out;
}

// DB-integrated scan: inserts/updates tracks with deduplication & metadata preservation
export std::expected<void, caudio::utils::Error>
scanLibrary(Database& db, int64_t libraryId,
            std::function<void(int64_t, int64_t, std::string_view)> progress = {}) {
    if (libraryId == 0)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
    auto libs = db.libraryList();
    if (!libs)
        return std::unexpected{libs.error()};
    std::string libPath;
    for (auto& l : *libs)
        if (l.id == libraryId)
            libPath = l.path;
    if (libPath.empty()) {
        // fallback: library id 1 with empty path is invalid
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::Result::NotFound, "library not found")};
    }
    std::filesystem::path root(libPath);
    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::Result::NotFound, "path not found")};
    int64_t scanned = 0;
    // single transaction for bulk? we do per-file with lock inside db methods, so just iterate
    for (auto trk : scan(root, ScanMode::Sampled)) {
        // early-exit check path+size+mtime
        auto existingPath = db.findByPath(trk.path);
        if (existingPath && existingPath->size == trk.size && existingPath->mtime == trk.mtime) {
            scanned++;
            if (progress)
                progress(scanned, 0, trk.path);
            continue;
        }
        // fingerprint dedup
        auto byFp = db.findByFingerprint(trk.fingerprint);
        if (byFp) {
            // update path/size/mtime if duplicate fingerprint found elsewhere
            Track upd = *byFp;
            upd.path = trk.path;
            upd.size = trk.size;
            upd.mtime = trk.mtime;
            upd.library_id = libraryId;
            upd.deleted_at = 0;
            (void)db.updateTrack(upd);
            // delete orphan duplicate path only if fingerprint also matches to avoid collision
            // delete if existingPath exists with different id but same fingerprint, delete it is
            // already handled by update? For orphan path with same path but different id, we
            // already updated the fingerprint holder, need to remove duplicate row if exists
            if (existingPath && existingPath->id != byFp->id) {
                // only delete if fingerprint matches
                bool same = true;
                for (int i = 0; i < 32; i++)
                    if (existingPath->fingerprint[i] != trk.fingerprint[i])
                        same = false;
                if (same)
                    (void)db.deleteTrack(existingPath->id);
            }
        } else if (existingPath) {
            // same path content changed: clear stale metadata preserve play_count/rating
            Track upd = *existingPath;
            int64_t keepPlay = upd.play_count;
            int keepRating = upd.rating;
            int64_t keepAdded = upd.date_added;
            [[maybe_unused]] std::array<uint8_t, 32> oldFp = upd.fingerprint;
            upd.fingerprint = trk.fingerprint;
            upd.size = trk.size;
            upd.mtime = trk.mtime;
            upd.library_id = libraryId;
            upd.deleted_at = 0;
            upd.title.clear();
            upd.artist.clear();
            upd.album.clear();
            upd.albumArtist.clear();
            upd.genre.clear();
            upd.year = 0;
            upd.track_num = 0;
            upd.disc_num = 0;
            upd.cover_art_path.clear();
            upd.duration = 0;
            upd.sample_rate = 0;
            upd.channels = 0;
            upd.bitrate = 0;
            upd.dirty = 0;
            upd.play_count = keepPlay;
            upd.rating = keepRating;
            upd.date_added = keepAdded;
            (void)db.updateTrack(upd);
        } else {
            trk.library_id = libraryId;
            (void)db.insertTrack(trk);
        }
        scanned++;
        if (progress)
            progress(scanned, 0, trk.path);
    }
    // update library last_scanned
    auto libs2 = db.libraryList();
    if (libs2) {
        for (auto& l : *libs2)
            if (l.id == libraryId) {
                l.last_scanned = std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
                (void)db.libraryUpdate(l);
                break;
            }
    }
    return {};
}

} // namespace caudio::db
