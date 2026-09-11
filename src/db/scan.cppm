module;
#include <sqlite3.h>

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
#include <mutex>
#include <shared_mutex>
#include <string>
#include <system_error>
#include <vector>

#include "blake3.h"

export module caudio.db:scan;

import caudio.utils;
import :types;
import :detail;
import :database;

namespace caudio::db {

export enum class ScanMode { Sampled, Full };

namespace detail {

inline bool hasAudioExt(const std::filesystem::path& p) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".mp3" || ext == ".flac" || ext == ".ogg" || ext == ".wav" || ext == ".m4a";
}

} // namespace detail

// Generator-based scan: yields Tracks lazily
export std::generator<Track> scan(const std::filesystem::path& root,
                                  ScanMode mode = ScanMode::Sampled) {
    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
        co_return;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file(ec) && detail::hasAudioExt(it->path())) {
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
                auto fp = detail::computeFingerprint(it->path());
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
    // Scan batches to avoid 30k lock hops and ensures crash atomicity per batch.
    // We hold Database::mutex() as unique_lock<shared_mutex> for the batch duration
    // and use BEGIN IMMEDIATE / COMMIT per 500 files so concurrent queueList never
    // sees partial state and a crash leaves DB consistent per batch.
    constexpr size_t kBatchSize = 500;
    size_t batchPending = 0;
    std::unique_lock<std::shared_mutex> batchLock;
    bool inTx = false;
    auto beginBatch = [&]() -> std::expected<void, caudio::utils::Error> {
        if (inTx)
            return {};
        batchLock = std::unique_lock<std::shared_mutex>(db.mutex());
        if (!db.handleLocked())
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        char* err = nullptr;
        detail::SqliteErrGuard guard{err};
        int rc = sqlite3_exec(db.handleLocked(), "BEGIN IMMEDIATE", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            batchLock.unlock();
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::Result::Busy, err ? std::string(err) : "BEGIN failed")};
        }
        inTx = true;
        batchPending = 0;
        return {};
    };
    auto commitBatch = [&]() -> std::expected<void, caudio::utils::Error> {
        if (!inTx)
            return {};
        char* err = nullptr;
        detail::SqliteErrGuard guard{err};
        int rc = sqlite3_exec(db.handleLocked(), "COMMIT", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            sqlite3_exec(db.handleLocked(), "ROLLBACK", nullptr, nullptr, nullptr);
            batchLock.unlock();
            inTx = false;
            batchPending = 0;
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::Result::Internal, err ? std::string(err) : "commit failed")};
        }
        batchLock.unlock();
        inTx = false;
        batchPending = 0;
        return {};
    };
    auto rollbackBatch = [&]() {
        if (!inTx)
            return;
        sqlite3_exec(db.handleLocked(), "ROLLBACK", nullptr, nullptr, nullptr);
        batchLock.unlock();
        inTx = false;
        batchPending = 0;
    };

    for (auto trk : scan(root, ScanMode::Sampled)) {
        if (!inTx) {
            auto b = beginBatch();
            if (!b)
                return std::unexpected{b.error()};
        }
        // early-exit check path+size+mtime — within batch tx via Locked helpers
        auto existingPath = db.findByPathLocked(trk.path);
        if (existingPath && existingPath->size == trk.size && existingPath->mtime == trk.mtime) {
            scanned++;
            batchPending++;
            if (progress)
                progress(scanned, 0, trk.path);
            if (batchPending >= kBatchSize) {
                auto c = commitBatch();
                if (!c) {
                    rollbackBatch();
                    return std::unexpected{c.error()};
                }
            }
            continue;
        }
        // fingerprint dedup
        auto byFp = db.findByFingerprintLocked(trk.fingerprint);
        if (byFp) {
            Track upd = *byFp;
            upd.path = trk.path;
            upd.size = trk.size;
            upd.mtime = trk.mtime;
            upd.library_id = libraryId;
            upd.deleted_at = 0;
            (void)db.updateTrackLocked(upd);
            if (existingPath && existingPath->id != byFp->id) {
                bool same = true;
                for (int i = 0; i < 32; i++)
                    if (existingPath->fingerprint[i] != trk.fingerprint[i])
                        same = false;
                if (same)
                    (void)db.deleteTrackLocked(existingPath->id);
            }
        } else if (existingPath) {
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
            (void)db.updateTrackLocked(upd);
        } else {
            trk.library_id = libraryId;
            (void)db.insertTrackLocked(trk);
        }
        scanned++;
        batchPending++;
        if (progress)
            progress(scanned, 0, trk.path);
        if (batchPending >= kBatchSize) {
            auto c = commitBatch();
            if (!c) {
                rollbackBatch();
                return std::unexpected{c.error()};
            }
        }
    }
    if (inTx) {
        auto c = commitBatch();
        if (!c) {
            rollbackBatch();
            return std::unexpected{c.error()};
        }
    }
    // update library last_scanned — in its own transaction via libraryUpdate (or locked if needed)
    auto libs2 = db.libraryList();
    if (libs2) {
        for (auto& l : *libs2)
            if (l.id == libraryId) {
                l.last_scanned = std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
                // ensure atomic update without exposing partial scan state: use a short transaction
                {
                    std::unique_lock<std::shared_mutex> lk(db.mutex());
                    char* err = nullptr;
                    detail::SqliteErrGuard guard{err};
                    int rc = sqlite3_exec(db.handleLocked(), "BEGIN IMMEDIATE", nullptr, nullptr, &err);
                    if (rc == SQLITE_OK) {
                        (void)db.libraryUpdateLocked(l);
                        char* cErr = nullptr;
                        detail::SqliteErrGuard cGuard{cErr};
                        rc = sqlite3_exec(db.handleLocked(), "COMMIT", nullptr, nullptr, &cErr);
                        if (rc != SQLITE_OK)
                            sqlite3_exec(db.handleLocked(), "ROLLBACK", nullptr, nullptr, nullptr);
                    } else {
                        (void)db.libraryUpdate(l);
                    }
                }
                break;
            }
    }
    return {};
}

} // namespace caudio::db
