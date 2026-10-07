#include <blake3.h>
#include <sqlite3.h>

#include <array>
#include <caudio/db/core.hpp>
#include <caudio/db/scan.hpp>
#include <caudio/db/types.hpp>
#include <caudio/player/decoder.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/generator.hpp>
#include <caudio/utils/result.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <db/fingerprint.hpp>
#include <db/stmt_helpers.hpp>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace caudio::db {

caudio::utils::Generator<Track> scan(const std::filesystem::path& root, ScanMode mode) {
    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
        co_return;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file(ec) && detail::hasAudioExt(it->path())) {
            Track t;
            t.path = it->path().generic_string();
            std::error_code e2;
            auto sz = it->file_size(e2);
            if (!e2)
                t.size = (int64_t)sz;
            auto ftime = it->last_write_time(e2);
            if (!e2)
                t.mtime = (int64_t)ftime.time_since_epoch().count();
            if (mode == ScanMode::Sampled) {
                auto fp = internal::computeFingerprint(it->path());
                if (fp)
                    t.fingerprint = *fp;
            } else {
                std::ifstream f(it->path(), std::ios::binary);
                if (f) {
                    blake3_hasher hasher;
                    blake3_hasher_init(&hasher);
                    std::array<std::byte, 8192> buf{};
                    while (f.read(reinterpret_cast<char*>(buf.data()),
                                  static_cast<std::streamsize>(buf.size())) ||
                           f.gcount())
                        blake3_hasher_update(&hasher, buf.data(), static_cast<size_t>(f.gcount()));
                    blake3_hasher_finalize(&hasher, t.fingerprint.data(), t.fingerprint.size());
                }
            }
            if (auto meta = caudio::player::extractMetadata(t.path); meta) {
                t.title = std::move(meta->title);
                t.artist = std::move(meta->artist);
                t.album = std::move(meta->album);
                t.album_artist = std::move(meta->album_artist);
                t.genre = std::move(meta->genre);
                t.year = meta->year;
                t.track_num = meta->track_num;
                t.disc_num = meta->disc_num;
                t.duration = meta->duration;
                t.sample_rate = static_cast<uint32_t>(meta->sample_rate);
                t.channels = static_cast<uint32_t>(meta->channels);
                t.bitrate = meta->bitrate;
            }
            co_yield t;
        }
    }
}

std::expected<std::vector<Track>, caudio::utils::Error>
scanDirectory(const std::filesystem::path& root, ScanMode mode) {
    std::vector<Track> out;
    for (auto t : scan(root, mode))
        out.push_back(std::move(t));
    return out;
}

std::expected<void, caudio::utils::Error>
scanLibrary(Database& db, int64_t libraryId,
            std::function<void(int64_t, int64_t, std::string_view)> progress, ScanMode mode) {
    if (libraryId == 0)
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
    auto libs = db.libraryList();
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
    int64_t scanned = 0;
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

    for (auto trk : scan(root, mode)) {
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
            Track upd = *byFp;
            upd.path = trk.path;
            upd.size = trk.size;
            upd.mtime = trk.mtime;
            upd.library_id = libraryId;
            upd.deleted_at = 0;
            bool hasMeta = false;
            if (auto meta = caudio::player::extractMetadata(trk.path); meta) {
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
            (void)db.updateTrackLocked(upd);
            if (existingPath && existingPath->id != byFp->id) {
                if (existingPath->fingerprint == trk.fingerprint)
                    (void)db.deleteTrackLocked(existingPath->id);
            }
        } else if (existingPath) {
            Track upd = *existingPath;
            int64_t keepPlay = upd.play_count;
            int keepRating = upd.rating;
            int64_t keepAdded = upd.date_added;
            upd.fingerprint = trk.fingerprint;
            upd.size = trk.size;
            upd.mtime = trk.mtime;
            upd.library_id = libraryId;
            upd.deleted_at = 0;
            bool hasMeta = false;
            if (auto meta = caudio::player::extractMetadata(trk.path); meta) {
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
    auto libs2 = db.libraryList();
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
