#include <sqlite3.h>

#include <caudio/db/database.hpp>
#include <caudio/db/scan.hpp>
#include <caudio/db/types.hpp>
#include <caudio/media/decoder.hpp>
#include <caudio/media/scan.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/log.hpp>
#include <caudio/utils/result.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <db/stmt_helpers.hpp>
#include <expected>
#include <filesystem>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace caudio::db {

std::expected<void, caudio::utils::Error>
scanLibrary(Database& db, int64_t libraryId,
            std::function<void(int64_t, int64_t, std::string_view)> progress,
            caudio::media::ScanMode mode) {
    if (libraryId == 0)
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
    auto libs = db.listLibraries();
    if (!libs)
        return std::unexpected{libs.error()};
    std::string libPath;
    for (auto& l : *libs)
        if (l.id == libraryId)
            libPath = l.path;
    if (libPath.empty()) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::NotFound, "library not found")};
    }
    std::filesystem::path root(libPath);
    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::NotFound, "path not found")};
    if (auto* lg = db.logger())
        lg->info("scan started root={} mode={}", libPath,
                 mode == caudio::media::ScanMode::Sampled ? "sampled" : "full");
    int64_t scanned = 0;
    int64_t added = 0;
    int64_t updated = 0;
    int64_t failed = 0;
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
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        char* err = nullptr;
        internal::SqliteErrGuard guard{err};
        int rc = sqlite3_exec(db.handleLocked(), "BEGIN IMMEDIATE", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            batchLock.unlock();
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Busy, err ? std::string(err) : "BEGIN failed")};
        }
        inTx = true;
        batchPending = 0;
        return {};
    };
    auto commitBatch = [&]() -> std::expected<void, caudio::utils::Error> {
        if (!inTx)
            return {};
        char* err = nullptr;
        internal::SqliteErrGuard guard{err};
        int rc = sqlite3_exec(db.handleLocked(), "COMMIT", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            sqlite3_exec(db.handleLocked(), "ROLLBACK", nullptr, nullptr, nullptr);
            batchLock.unlock();
            inTx = false;
            batchPending = 0;
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Internal, err ? std::string(err) : "commit failed")};
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

    for (auto trk : caudio::media::scanTracks(root, mode)) {
        if (!inTx) {
            auto b = beginBatch();
            if (!b)
                return std::unexpected{b.error()};
        }
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
        auto byFp = db.findByFingerprintLocked(trk.fingerprint);
        if (byFp) {
            caudio::types::Track upd = *byFp;
            upd.path = trk.path;
            upd.size = trk.size;
            upd.mtime = trk.mtime;
            upd.library_id = libraryId;
            upd.deleted_at = 0;
            bool hasMeta = false;
            auto meta = caudio::media::extractMetadata(trk.path);
            if (!meta) {
                if (auto* lg = db.logger())
                    lg->debug("scan metadata failed file={} err={}", trk.path,
                              meta.error().message);
            }
            if (meta) {
                if (!meta->title.empty()) {
                    upd.title = std::move(meta->title);
                    hasMeta = true;
                }
                if (!meta->artist.empty()) {
                    upd.artist = std::move(meta->artist);
                    hasMeta = true;
                }
                if (!meta->album.empty()) {
                    upd.album = std::move(meta->album);
                    hasMeta = true;
                }
                if (!meta->album_artist.empty()) {
                    upd.album_artist = std::move(meta->album_artist);
                    hasMeta = true;
                }
                if (!meta->genre.empty()) {
                    upd.genre = std::move(meta->genre);
                    hasMeta = true;
                }
                if (meta->year != 0) {
                    upd.year = meta->year;
                    hasMeta = true;
                }
                if (meta->track_num != 0) {
                    upd.track_num = meta->track_num;
                    hasMeta = true;
                }
                if (meta->disc_num != 0) {
                    upd.disc_num = meta->disc_num;
                    hasMeta = true;
                }
                if (meta->duration > 0) {
                    upd.duration = meta->duration;
                    hasMeta = true;
                }
                if (meta->sample_rate != 0) {
                    upd.sample_rate = static_cast<uint32_t>(meta->sample_rate);
                    hasMeta = true;
                }
                if (meta->channels != 0) {
                    upd.channels = static_cast<uint32_t>(meta->channels);
                    hasMeta = true;
                }
                if (meta->bitrate != 0) {
                    upd.bitrate = meta->bitrate;
                    hasMeta = true;
                }
            }
            if (!hasMeta) {
                upd.title.clear();
                upd.artist.clear();
                upd.album.clear();
                upd.album_artist.clear();
                upd.genre.clear();
                upd.year = 0;
                upd.track_num = 0;
                upd.disc_num = 0;
                upd.cover_art_path.clear();
                upd.duration = 0;
                upd.sample_rate = 0;
                upd.channels = 0;
                upd.bitrate = 0;
            }
            if (db.updateTrackLocked(upd))
                updated++;
            else
                failed++;
            if (existingPath && existingPath->id != byFp->id) {
                if (existingPath->fingerprint == trk.fingerprint)
                    (void)db.deleteTrackLocked(existingPath->id);
            }
        } else if (existingPath) {
            caudio::types::Track upd = *existingPath;
            int64_t keepPlay = upd.play_count;
            int keepRating = upd.rating;
            int64_t keepAdded = upd.date_added;
            upd.fingerprint = trk.fingerprint;
            upd.size = trk.size;
            upd.mtime = trk.mtime;
            upd.library_id = libraryId;
            upd.deleted_at = 0;
            bool hasMeta = false;
            auto meta = caudio::media::extractMetadata(trk.path);
            if (!meta) {
                if (auto* lg = db.logger())
                    lg->debug("scan metadata failed file={} err={}", trk.path,
                              meta.error().message);
            }
            if (meta) {
                if (!meta->title.empty()) {
                    upd.title = std::move(meta->title);
                    hasMeta = true;
                }
                if (!meta->artist.empty()) {
                    upd.artist = std::move(meta->artist);
                    hasMeta = true;
                }
                if (!meta->album.empty()) {
                    upd.album = std::move(meta->album);
                    hasMeta = true;
                }
                if (!meta->album_artist.empty()) {
                    upd.album_artist = std::move(meta->album_artist);
                    hasMeta = true;
                }
                if (!meta->genre.empty()) {
                    upd.genre = std::move(meta->genre);
                    hasMeta = true;
                }
                if (meta->year != 0) {
                    upd.year = meta->year;
                    hasMeta = true;
                }
                if (meta->track_num != 0) {
                    upd.track_num = meta->track_num;
                    hasMeta = true;
                }
                if (meta->disc_num != 0) {
                    upd.disc_num = meta->disc_num;
                    hasMeta = true;
                }
                if (meta->duration > 0) {
                    upd.duration = meta->duration;
                    hasMeta = true;
                }
                if (meta->sample_rate != 0) {
                    upd.sample_rate = static_cast<uint32_t>(meta->sample_rate);
                    hasMeta = true;
                }
                if (meta->channels != 0) {
                    upd.channels = static_cast<uint32_t>(meta->channels);
                    hasMeta = true;
                }
                if (meta->bitrate != 0) {
                    upd.bitrate = meta->bitrate;
                    hasMeta = true;
                }
            }
            if (!hasMeta) {
                upd.title.clear();
                upd.artist.clear();
                upd.album.clear();
                upd.album_artist.clear();
                upd.genre.clear();
                upd.year = 0;
                upd.track_num = 0;
                upd.disc_num = 0;
                upd.cover_art_path.clear();
                upd.duration = 0;
                upd.sample_rate = 0;
                upd.channels = 0;
                upd.bitrate = 0;
            }
            upd.dirty = false;
            upd.play_count = keepPlay;
            upd.rating = keepRating;
            upd.date_added = keepAdded;
            if (db.updateTrackLocked(upd))
                updated++;
            else
                failed++;
        } else {
            trk.library_id = libraryId;
            if (db.insertTrackLocked(trk))
                added++;
            else
                failed++;
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
    if (auto* lg = db.logger())
        lg->info("scan finished root={} scanned={} added={} updated={} failed={}", libPath, scanned,
                 added, updated, failed);
    auto libs2 = db.listLibraries();
    if (libs2) {
        for (auto& l : *libs2)
            if (l.id == libraryId) {
                l.last_scanned = std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
                {
                    std::unique_lock<std::shared_mutex> lk(db.mutex());
                    char* err = nullptr;
                    internal::SqliteErrGuard guard{err};
                    int rc =
                        sqlite3_exec(db.handleLocked(), "BEGIN IMMEDIATE", nullptr, nullptr, &err);
                    if (rc == SQLITE_OK) {
                        (void)db.libraryUpdateLocked(l);
                        char* cErr = nullptr;
                        internal::SqliteErrGuard cGuard{cErr};
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
