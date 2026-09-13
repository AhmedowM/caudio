module;
#include <sqlite3.h>

#include <cstring>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

export module caudio.db:core;

import caudio.utils;
import :types;
import :schema;
import :queue;
import :write_thread;
import :SqliteStatement;
import :DbTransaction;
import :detail;

export namespace caudio::db {

struct DbOpts {
    std::size_t writeBatchSize = 256;
};

class Database final {
  public:
    Database() = default;
    explicit Database(const DbOpts& opts) : writer_(opts.writeBatchSize) {}
    ~Database() {
        writer_.close();
        {
            std::lock_guard lk{cacheMutex_};
            stmtCache_.clear();
        }
        db_.reset();
    }
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    // Move is not thread-safe after open: caller must ensure no concurrent DB access.
    // If move is used, lock order must be dbMutex_ (dbMutex_) -> stmtCacheMutex_ (cacheMutex_)
    // to match normal operation (unique_lock dbMutex_ then cacheMutex_).
    Database(Database&& o) noexcept : writer_(std::move(o.writer_)), db_(std::move(o.db_)) {
        // cache stays empty for moved-from; moved-to starts empty (statements tied to old handle)
        // if o had cached stmts they are cleared (handle moved)
        // Acquire in documented order: dbMutex_ -> stmtCacheMutex_
        {
            std::unique_lock lkDb{o.dbMutex_, std::defer_lock};
            std::unique_lock lkCache{o.cacheMutex_, std::defer_lock};
            std::lock(lkDb, lkCache);
            o.stmtCache_.clear();
        }
    }
    Database& operator=(Database&& o) noexcept {
        if (this != &o) {
            // Documented lock order: dbMutex_ -> stmtCacheMutex_. Move after open is
            // discouraged; if used, caller must quiesce. We acquire both sides in order.
            std::unique_lock lkThisDb{dbMutex_, std::defer_lock};
            std::unique_lock lkThisCache{cacheMutex_, std::defer_lock};
            std::unique_lock lkODb{o.dbMutex_, std::defer_lock};
            std::unique_lock lkOCache{o.cacheMutex_, std::defer_lock};
            std::lock(lkThisDb, lkThisCache, lkODb, lkOCache);
            writer_.close();
            stmtCache_.clear();
            db_.reset();
            writer_ = std::move(o.writer_);
            db_ = std::move(o.db_);
            o.stmtCache_.clear();
            // also clear any remaining in *this (already cleared) - start fresh for new handle
        }
        return *this;
    }
    void clearCache() const {
        std::lock_guard lk{cacheMutex_};
        stmtCache_.clear();
    }
    // per-connection prepared SqliteStatement cache
    // Primary API: getCachedForUse returns expected; callers must check.
    // Legacy raw-pointer APIs are deprecated and delegate to expected (nullptr only on error).
    std::expected<SqliteStatement*, caudio::utils::Error>
    getCachedForUse(std::string_view sql) const {
        // assumes stmtCacheMutex_ (cacheMutex_) already held by caller
        std::string key(sql);
        auto it = stmtCache_.find(key);
        if (it != stmtCache_.end()) {
            it->second->reset();
            return it->second.get();
        }
        auto up = std::make_unique<SqliteStatement>();
        if (auto e = up->prepare(db_.get(), sql); !e)
            return std::unexpected(e.error());
        SqliteStatement* raw = up.get();
        stmtCache_.emplace(std::move(key), std::move(up));
        return raw;
    }
    [[deprecated("use getCachedForUse; raw nullptr is error — check expected")]]
    SqliteStatement* getCachedLocked(std::string_view sql) const {
        // Caller must hold stmtCacheMutex_ (cacheMutex_); delegate to expected API.
        auto r = getCachedForUse(sql);
        if (!r)
            return nullptr; // error — caller of deprecated API must check nullptr
        return *r;
    }
    [[deprecated("use getCachedForUse with external lock; raw nullptr is error")]]
    SqliteStatement* getCached(const std::string& sql) const {
        std::lock_guard lk{cacheMutex_};
        auto r = getCachedForUse(sql);
        if (!r)
            return nullptr; // error — see deprecation note
        return *r;
    }
    static std::expected<std::unique_ptr<Database>, caudio::utils::Error>
    open(std::string_view path, const DbOpts& opts = {}) {
        std::string dbPath = path.empty() ? ":memory:" : std::string(path);
        sqlite3* raw = nullptr;
        int rc =
            sqlite3_open_v2(dbPath.c_str(), &raw,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI, nullptr);
        if (rc != SQLITE_OK) {
            std::string msg = raw ? sqlite3_errmsg(raw) : "open failed";
            if (raw)
                sqlite3_close(raw);
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, msg)};
        }
        char* err = nullptr;
        internal::SqliteErrGuard errGuard{err};
        rc = sqlite3_exec(raw, std::string(kSchema).c_str(), nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? std::string(err) : sqlite3_errmsg(raw);
            // Gracefully handle existing duplicate queue positions on migration:
            // UNIQUE(queue_id, position) creation may fail if old DB has duplicates.
            // Treat as non-fatal — open still succeeds; queue ops will normalize positions.
            bool isQueueUniqueMigration = msg.find("idx_queue_queue_pos") != std::string::npos ||
                                          msg.find("queue") != std::string::npos;
            bool isUniqueConstraint =
                msg.find("UNIQUE") != std::string::npos || msg.find("unique") != std::string::npos;
            if (!(isQueueUniqueMigration && isUniqueConstraint)) {
                sqlite3_close(raw);
                return std::unexpected{caudio::utils::makeError(
                    caudio::utils::StatusCode::Corrupt, std::string("schema init failed: ") + msg)};
            }
            // else: non-fatal migration duplicate — clear err for next exec
            if (err) {
                sqlite3_free(err);
                err = nullptr;
            }
        }
        rc = sqlite3_exec(raw, std::string(kSchemaDefaultLibrary).c_str(), nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? std::string(err) : sqlite3_errmsg(raw);
            // not fatal? but log
            if (err) {
                sqlite3_free(err);
                err = nullptr;
            }
        }
        // Migration: add active_queue_id column if missing (for existing DBs)
        {
            char* migErr = nullptr;
            int migRc = sqlite3_exec(raw,
                                     "ALTER TABLE engine_state ADD COLUMN active_queue_id INTEGER DEFAULT 1",
                                     nullptr, nullptr, &migErr);
            if (migErr) {
                sqlite3_free(migErr);
                migErr = nullptr;
            }
            (void)migRc;
        }
        auto db = std::make_unique<Database>(opts);
        db->db_.reset(raw);
        db->writer_.open(raw);
        return db;
    }
    sqlite3* handle() const {
        return db_.get();
    }
    std::shared_mutex& mutex() const {
        return dbMutex_;
    }
    sqlite3* handleLocked() const noexcept {
        return db_.get();
    }
    std::expected<void, caudio::utils::Error> flush() {
        return writer_.flush();
    }

    // Generic DbTransaction helper — BEGIN IMMEDIATE / COMMIT / ROLLBACK
    // Holds Database::mutex() (dbMutex_ dbMutex_) for duration.
    template <typename Fn>
    std::expected<void, caudio::utils::Error> withTransaction(Fn&& fn) {
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        char* err = nullptr;
        internal::SqliteErrGuard guard{err};
        int rc = sqlite3_exec(db_.get(), "BEGIN IMMEDIATE", nullptr, nullptr, &err);
        if (rc != SQLITE_OK)
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Busy, err ? std::string(err) : "BEGIN failed")};
        auto res = fn(db_.get());
        if (!res) {
            sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            return res;
        }
        char* cErr = nullptr;
        internal::SqliteErrGuard cGuard{cErr};
        rc = sqlite3_exec(db_.get(), "COMMIT", nullptr, nullptr, &cErr);
        if (rc != SQLITE_OK) {
            sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Internal, cErr ? std::string(cErr) : "commit failed")};
        }
        return {};
    }

    // Batch queue helpers — single DbTransaction, atomic to concurrent queueList
    std::expected<void, caudio::utils::Error> queueEnqueueBatch(int64_t qid,
                                                                std::span<const int64_t> tids) {
        if (qid == 0)
            qid = 1;
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        char* err = nullptr;
        internal::SqliteErrGuard guard{err};
        int rc = sqlite3_exec(db_.get(), "BEGIN IMMEDIATE", nullptr, nullptr, &err);
        if (rc != SQLITE_OK)
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Busy, err ? std::string(err) : "BEGIN failed")};
        for (int64_t tid : tids) {
            auto r = caudio::db::queueEnqueueLocked(db_.get(), qid, tid, -1);
            if (!r) {
                sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
                return std::unexpected{r.error()};
            }
        }
        char* cErr = nullptr;
        internal::SqliteErrGuard cGuard{cErr};
        rc = sqlite3_exec(db_.get(), "COMMIT", nullptr, nullptr, &cErr);
        if (rc != SQLITE_OK) {
            sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Internal, cErr ? std::string(cErr) : "commit failed")};
        }
        return {};
    }
    std::expected<void, caudio::utils::Error> queueEnqueueBatch(int64_t qid,
                                                                const std::vector<int64_t>& tids) {
        return queueEnqueueBatch(qid, std::span<const int64_t>(tids.data(), tids.size()));
    }

    std::expected<void, caudio::utils::Error> queueReplaceAll(int64_t qid,
                                                              std::span<const int64_t> ids) {
        if (qid == 0)
            qid = 1;
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        char* err = nullptr;
        internal::SqliteErrGuard guard{err};
        int rc = sqlite3_exec(db_.get(), "BEGIN IMMEDIATE", nullptr, nullptr, &err);
        if (rc != SQLITE_OK)
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Busy, err ? std::string(err) : "BEGIN failed")};
        auto clr = caudio::db::queueClearLocked(db_.get(), qid);
        if (!clr) {
            sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            return std::unexpected{clr.error()};
        }
        for (int64_t id : ids) {
            auto r = caudio::db::queueEnqueueLocked(db_.get(), qid, id, -1);
            if (!r) {
                sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
                return std::unexpected{r.error()};
            }
        }
        char* cErr = nullptr;
        internal::SqliteErrGuard cGuard{cErr};
        rc = sqlite3_exec(db_.get(), "COMMIT", nullptr, nullptr, &cErr);
        if (rc != SQLITE_OK) {
            sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Internal, cErr ? std::string(cErr) : "commit failed")};
        }
        return {};
    }
    std::expected<void, caudio::utils::Error> queueReplaceAll(int64_t qid,
                                                              const std::vector<int64_t>& ids) {
        return queueReplaceAll(qid, std::span<const int64_t>(ids.data(), ids.size()));
    }

    // Locked track/library helpers — caller must hold Database::mutex() (dbMutex_ dbMutex_)
    // These avoid re-locking dbMutex_ and are intended for use inside outer transactions/batches.
    std::expected<Track, caudio::utils::Error> findByPathLocked(std::string_view path) {
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        std::string sql = std::string(internal::kSelectTracksCols) + " WHERE path=?";
        std::unique_lock cacheLk{cacheMutex_};
        auto sRes = getCachedForUse(sql);
        if (!sRes)
            return std::unexpected{sRes.error()};
        SqliteStatement& st = *(*sRes);
        st.bindText(1, path);
        bool hasRow = st.step();
        Track t;
        if (hasRow)
            internal::fillTrackFromStmt(st.get(), t);
        st.reset();
        if (!hasRow)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        return t;
    }
    std::expected<Track, caudio::utils::Error>
    findByFingerprintLocked(const std::array<uint8_t, 32>& fp) {
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        std::string sql = std::string(internal::kSelectTracksCols) + " WHERE fingerprint=?";
        std::unique_lock cacheLk{cacheMutex_};
        auto sRes = getCachedForUse(sql);
        if (!sRes)
            return std::unexpected{sRes.error()};
        SqliteStatement& st = *(*sRes);
        st.bindBlob(1, std::span<const std::byte>{reinterpret_cast<const std::byte*>(fp.data()),
                                                  fp.size()});
        bool hasRow = st.step();
        Track t;
        if (hasRow)
            internal::fillTrackFromStmt(st.get(), t);
        st.reset();
        if (!hasRow)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        return t;
    }
    std::expected<int64_t, caudio::utils::Error> insertTrackLocked(const Track& t) {
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        std::unique_lock cacheLk{cacheMutex_};
        auto sRes = getCachedForUse(kInsertTrackSql);
        if (!sRes)
            return std::unexpected{sRes.error()};
        SqliteStatement& st = *(*sRes);
        bindTrackForInsert(st, t);
        int rc = st.stepDone();
        st.reset();
        if (rc != SQLITE_DONE) {
            int ec = sqlite3_extended_errcode(db_.get());
            if (ec == SQLITE_CONSTRAINT_UNIQUE || rc == SQLITE_CONSTRAINT)
                return std::unexpected{caudio::utils::makeError(
                    caudio::utils::StatusCode::AlreadyExists, sqlite3_errmsg(db_.get()))};
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        }
        return sqlite3_last_insert_rowid(db_.get());
    }
    std::expected<void, caudio::utils::Error> updateTrackLocked(const Track& t) {
        if (t.id == 0)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "id 0")};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        std::unique_lock cacheLk{cacheMutex_};
        auto sRes = getCachedForUse(kUpdateTrackSql);
        if (!sRes)
            return std::unexpected{sRes.error()};
        SqliteStatement& st = *(*sRes);
        bindTrackForUpdate(st, t);
        int rc = st.stepDone();
        st.reset();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        if (sqlite3_changes(db_.get()) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        return {};
    }
    std::expected<void, caudio::utils::Error> deleteTrackLocked(int64_t id) {
        if (id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        std::string_view sql = "DELETE FROM tracks WHERE id=?";
        std::unique_lock cacheLk{cacheMutex_};
        auto sRes = getCachedForUse(sql);
        if (!sRes)
            return std::unexpected{sRes.error()};
        SqliteStatement& st = *(*sRes);
        st.bindInt(1, id);
        int rc = st.stepDone();
        st.reset();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        if (sqlite3_changes(db_.get()) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        return {};
    }
    std::expected<std::vector<Library>, caudio::utils::Error> libraryListLocked() {
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        SqliteStatement st;
        if (auto e =
                st.prepare(db_.get(), "SELECT id, path, name, date_added, last_scanned, auto_scan, "
                                      "recursive, extensions FROM libraries ORDER BY id");
            !e)
            return std::unexpected{e.error()};
        std::vector<Library> out;
        while (st.step()) {
            Library l;
            l.id = st.columnInt(0);
            l.path = st.columnText(1);
            l.name = st.columnText(2);
            l.date_added = st.columnInt(3);
            l.last_scanned = st.columnInt(4);
            l.auto_scan = st.columnInt(5) != 0;
            l.recursive = st.columnInt(6) != 0;
            l.extensions = st.columnText(7);
            out.push_back(std::move(l));
        }
        return out;
    }
    std::expected<void, caudio::utils::Error> libraryUpdateLocked(const Library& l) {
        if (l.id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        SqliteStatement st;
        if (auto e = st.prepare(db_.get(), "UPDATE libraries SET path=?, name=?, last_scanned=?, "
                                           "auto_scan=?, recursive=?, extensions=? WHERE id=?");
            !e)
            return std::unexpected{e.error()};
        st.bindText(1, l.path);
        if (l.name.empty())
            st.bindNull(2);
        else
            st.bindText(2, l.name);
        st.bindInt(3, l.last_scanned);
        st.bindInt(4, l.auto_scan ? 1 : 0);
        st.bindInt(5, l.recursive ? 1 : 0);
        st.bindText(6, l.extensions);
        st.bindInt(7, l.id);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        if (sqlite3_changes(db_.get()) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        return {};
    }

  private:
    WriterThread writer_;
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db_{nullptr, &sqlite3_close};
    // Naming clarity: dbMutex_ is dbMutex_ (protects db_ handle and serializes DB ops),
    // cacheMutex_ is stmtCacheMutex_ (protects stmtCache_), queueLock_ concept maps to
    // engineQueueSpin_ in engine but DB uses dbMutex_ for queue table as well.
    // Lock order: dbMutex_ (dbMutex_) -> stmtCacheMutex_ (cacheMutex_)
    mutable std::shared_mutex dbMutex_;
    mutable std::unordered_map<std::string, std::unique_ptr<SqliteStatement>> stmtCache_;
    mutable std::mutex cacheMutex_; // stmtCacheMutex_

  private:
    // Inline helpers — reduce duplication between insert/update (internal::bindTrack coverage)
    static void bindTrackForInsert(SqliteStatement& st, const Track& t) {
        st.bindBlob(
            1, std::span<const std::byte>{reinterpret_cast<const std::byte*>(t.fingerprint.data()),
                                          t.fingerprint.size()});
        st.bindText(2, t.path);
        st.bindInt(3, t.size);
        st.bindInt(4, t.mtime);
        st.bindDouble(5, t.duration);
        st.bindInt(6, t.sample_rate);
        st.bindInt(7, t.channels);
        st.bindInt(8, t.bitrate);
        st.bindText(9, t.title);
        st.bindText(10, t.artist);
        st.bindText(11, t.album);
        st.bindText(12, t.album_artist);
        st.bindText(13, t.genre);
        st.bindInt(14, t.year);
        st.bindInt(15, t.track_num);
        st.bindInt(16, t.disc_num);
        st.bindText(17, t.cover_art_path);
        st.bindInt(18, t.rating);
        st.bindInt(19, t.play_count);
        st.bindInt(20, t.last_played);
        if (t.date_added)
            st.bindInt(21, t.date_added);
        else
            st.bindNull(21);
        st.bindInt(22, t.last_scanned);
        st.bindInt(23, t.dirty ? 1 : 0);
        st.bindInt(24, t.library_id ? t.library_id : 1);
    }
    static void bindTrackForUpdate(SqliteStatement& st, const Track& t) {
        st.bindBlob(
            1, std::span<const std::byte>{reinterpret_cast<const std::byte*>(t.fingerprint.data()),
                                          t.fingerprint.size()});
        st.bindText(2, t.path);
        st.bindInt(3, t.size);
        st.bindInt(4, t.mtime);
        st.bindDouble(5, t.duration);
        st.bindInt(6, t.sample_rate);
        st.bindInt(7, t.channels);
        st.bindInt(8, t.bitrate);
        st.bindText(9, t.title);
        st.bindText(10, t.artist);
        st.bindText(11, t.album);
        st.bindText(12, t.album_artist);
        st.bindText(13, t.genre);
        st.bindInt(14, t.year);
        st.bindInt(15, t.track_num);
        st.bindInt(16, t.disc_num);
        st.bindText(17, t.cover_art_path);
        st.bindInt(18, t.rating);
        st.bindInt(19, t.play_count);
        st.bindInt(20, t.last_played);
        st.bindInt(21, t.date_added);
        st.bindInt(22, t.last_scanned);
        st.bindInt(23, t.dirty ? 1 : 0);
        st.bindInt(24, t.library_id ? t.library_id : 1);
        if (t.deleted_at)
            st.bindInt(25, t.deleted_at);
        else
            st.bindNull(25);
        st.bindInt(26, t.id);
    }
    static constexpr std::string_view kInsertTrackSql =
        "INSERT INTO tracks (fingerprint, path, size, mtime, duration, sample_rate, channels, "
        "bitrate, title, artist, album, album_artist, genre, year, track_num, disc_num, "
        "cover_art_path, rating, play_count, last_played, date_added, last_scanned, dirty, "
        "library_id) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    static constexpr std::string_view kUpdateTrackSql =
        "UPDATE tracks SET fingerprint=?, path=?, size=?, mtime=?, duration=?, sample_rate=?, "
        "channels=?, bitrate=?, title=?, artist=?, album=?, album_artist=?, genre=?, year=?, "
        "track_num=?, disc_num=?, cover_art_path=?, rating=?, play_count=?, last_played=?, "
        "date_added=?, last_scanned=?, dirty=?, library_id=?, deleted_at=? WHERE id=?";

  public:
    // Track CRUD
    std::expected<int64_t, caudio::utils::Error> insertTrack(const Track& t) {
        std::unique_lock lk{dbMutex_};
        return insertTrackLocked(t);
    }
    std::expected<void, caudio::utils::Error> updateTrack(const Track& t) {
        std::unique_lock lk{dbMutex_};
        return updateTrackLocked(t);
    }
    std::expected<void, caudio::utils::Error> deleteTrack(int64_t id) {
        std::unique_lock lk{dbMutex_};
        return deleteTrackLocked(id);
    }
    std::expected<Track, caudio::utils::Error> getTrack(int64_t id) {
        std::shared_lock lk{dbMutex_};
        return getTrackLocked(id);
    }

    std::expected<Track, caudio::utils::Error> getTrackLocked(int64_t id) {
        if (id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        std::string sql = std::string(internal::kSelectTracksCols) + " WHERE id=?";
        std::unique_lock cacheLk{cacheMutex_};
        auto sRes = getCachedForUse(sql);
        if (!sRes)
            return std::unexpected{sRes.error()};
        SqliteStatement& st = *(*sRes);
        st.bindInt(1, id);
        bool hasRow = st.step();
        Track t;
        if (hasRow)
            internal::fillTrackFromStmt(st.get(), t);
        st.reset();
        if (!hasRow)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        return t;
    }
    std::expected<Track, caudio::utils::Error>
    findByFingerprint(const std::array<uint8_t, 32>& fp) {
        std::shared_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        std::string sql = std::string(internal::kSelectTracksCols) + " WHERE fingerprint=?";
        std::unique_lock cacheLk{cacheMutex_};
        auto sRes = getCachedForUse(sql);
        if (!sRes)
            return std::unexpected{sRes.error()};
        SqliteStatement& st = *(*sRes);
        st.bindBlob(1, std::span<const std::byte>{reinterpret_cast<const std::byte*>(fp.data()),
                                                  fp.size()});
        bool hasRow = st.step();
        Track t;
        if (hasRow)
            internal::fillTrackFromStmt(st.get(), t);
        st.reset();
        if (!hasRow)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        return t;
    }
    std::expected<Track, caudio::utils::Error> findByPath(std::string_view path) {
        std::shared_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        std::string sql = std::string(internal::kSelectTracksCols) + " WHERE path=?";
        std::unique_lock cacheLk{cacheMutex_};
        auto sRes = getCachedForUse(sql);
        if (!sRes)
            return std::unexpected{sRes.error()};
        SqliteStatement& st = *(*sRes);
        st.bindText(1, path);
        bool hasRow = st.step();
        Track t;
        if (hasRow)
            internal::fillTrackFromStmt(st.get(), t);
        st.reset();
        if (!hasRow)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        return t;
    }
    std::expected<std::vector<Track>, caudio::utils::Error>
    listTracks(const TrackQuery* q = nullptr) {
        std::shared_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        std::string sql = std::string(internal::kSelectTracksCols) + " WHERE 1=1";
        if (q) {
            if (q->library_id)
                sql += " AND library_id=?";
            if (!q->artist.empty())
                sql += " AND artist=?";
            if (!q->album.empty())
                sql += " AND album=?";
            if (!q->genre.empty())
                sql += " AND genre=?";
            if (q->year)
                sql += " AND year=?";
            if (q->dirty)
                sql += " AND dirty=?";
            if (!q->search.empty())
                sql += " AND (title LIKE ? ESCAPE '\\' OR artist LIKE ? ESCAPE '\\' OR album LIKE "
                       "? ESCAPE '\\' OR genre LIKE ? ESCAPE '\\')";
        }
        sql += " ORDER BY id";
        bool has_limit = q && q->limit > 0;
        bool has_offset = q && q->offset > 0;
        if (has_limit) {
            sql += " LIMIT ?";
            if (has_offset)
                sql += " OFFSET ?";
        } else if (has_offset)
            sql += " LIMIT -1 OFFSET ?";
        SqliteStatement st;
        if (auto e = st.prepare(db_.get(), sql); !e)
            return std::unexpected{e.error()};
        int idx = 1;
        std::string likePat;
        if (q) {
            if (q->library_id)
                st.bindInt(idx++, *q->library_id);
            if (!q->artist.empty())
                st.bindText(idx++, q->artist);
            if (!q->album.empty())
                st.bindText(idx++, q->album);
            if (!q->genre.empty())
                st.bindText(idx++, q->genre);
            if (q->year)
                st.bindInt(idx++, *q->year);
            if (q->dirty)
                st.bindInt(idx++, *q->dirty ? 1 : 0);
            if (!q->search.empty()) {
                std::string esc = internal::escapeLike(q->search);
                likePat = "%" + esc + "%";
                st.bindText(idx++, likePat);
                st.bindText(idx++, likePat);
                st.bindText(idx++, likePat);
                st.bindText(idx++, likePat);
            }
            if (has_limit) {
                st.bindInt(idx++, q->limit);
                if (has_offset)
                    st.bindInt(idx++, q->offset);
            } else if (has_offset)
                st.bindInt(idx++, q->offset);
        }
        std::vector<Track> out;
        while (st.step()) {
            Track t;
            internal::fillTrackFromStmt(st.get(), t);
            out.push_back(std::move(t));
        }
        return out;
    }
    std::expected<void, caudio::utils::Error> setDirty(int64_t id, int dirty) {
        if (id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        std::string_view sql = "UPDATE tracks SET dirty=? WHERE id=?";
        std::unique_lock cacheLk{cacheMutex_};
        auto sRes = getCachedForUse(sql);
        if (!sRes)
            return std::unexpected{sRes.error()};
        SqliteStatement& st = *(*sRes);
        st.bindInt(1, dirty);
        st.bindInt(2, id);
        int rc = st.stepDone();
        st.reset();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        if (sqlite3_changes(db_.get()) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        return {};
    }

    // compat helpers for older database.cppm API
    [[deprecated("use getTrack; compat shim — remove in next major")]]
    std::expected<std::string, caudio::utils::Error> getTrackName(int64_t id) {
        auto r = getTrack(id);
        if (!r)
            return std::unexpected{r.error()};
        return r->title.empty() ? r->path : r->title;
    }
    [[deprecated("use listTracks; compat shim — remove in next major")]]
    std::expected<std::vector<std::tuple<int64_t, std::string>>, caudio::utils::Error>
    listTracksSimple(int64_t libraryId = 0) {
        TrackQuery q;
        if (libraryId > 0) {
            q.library_id = libraryId;
        }
        auto r = listTracks(&q);
        if (!r)
            return std::unexpected{r.error()};
        std::vector<std::tuple<int64_t, std::string>> out;
        for (auto& t : *r)
            out.emplace_back(t.id, t.title.empty() ? t.path : t.title);
        return out;
    }

    // Playlists
    std::expected<int64_t, caudio::utils::Error> createPlaylist(std::string_view name, int type = 0,
                                                                std::string_view smart_query = {}) {
        if (name.empty())
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        SqliteStatement st;
        if (auto e = st.prepare(db_.get(),
                                "INSERT INTO playlists (name, type, smart_query) VALUES (?,?,?)");
            !e)
            return std::unexpected{e.error()};
        st.bindText(1, name);
        st.bindInt(2, type);
        if (smart_query.empty())
            st.bindNull(3);
        else
            st.bindText(3, smart_query);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        return sqlite3_last_insert_rowid(db_.get());
    }
    std::expected<Playlist, caudio::utils::Error> getPlaylist(int64_t id) {
        if (id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        std::shared_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        SqliteStatement st;
        if (auto e = st.prepare(db_.get(), "SELECT id, name, type, smart_query, created, modified, "
                                           "library_id FROM playlists WHERE id=?");
            !e)
            return std::unexpected{e.error()};
        st.bindInt(1, id);
        if (!st.step())
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        Playlist p;
        p.id = st.columnInt(0);
        p.name = st.columnText(1);
        p.type = (int)st.columnInt(2);
        p.smart_query = st.columnText(3);
        p.created = st.columnInt(4);
        p.modified = st.columnInt(5);
        p.library_id = st.columnInt(6);
        return p;
    }
    std::expected<void, caudio::utils::Error> updatePlaylist(const Playlist& p) {
        if (p.id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        SqliteStatement st;
        if (auto e = st.prepare(db_.get(), "UPDATE playlists SET name=?, type=?, smart_query=?, "
                                           "library_id=?, modified=CURRENT_TIMESTAMP WHERE id=?");
            !e)
            return std::unexpected{e.error()};
        st.bindText(1, p.name);
        st.bindInt(2, p.type);
        if (p.smart_query.empty())
            st.bindNull(3);
        else
            st.bindText(3, p.smart_query);
        st.bindInt(4, p.library_id);
        st.bindInt(5, p.id);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        if (sqlite3_changes(db_.get()) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        return {};
    }
    std::expected<void, caudio::utils::Error> deletePlaylist(int64_t id) {
        if (id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        SqliteStatement st;
        if (auto e = st.prepare(db_.get(), "DELETE FROM playlists WHERE id=?"); !e)
            return std::unexpected{e.error()};
        st.bindInt(1, id);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        if (sqlite3_changes(db_.get()) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        return {};
    }
    std::expected<void, caudio::utils::Error> renamePlaylist(int64_t id, std::string_view newName) {
        if (id == 0 || newName.empty())
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        SqliteStatement st;
        if (auto e = st.prepare(db_.get(),
                                "UPDATE playlists SET name=?, modified=CURRENT_TIMESTAMP WHERE id=?");
            !e)
            return std::unexpected{e.error()};
        st.bindText(1, newName);
        st.bindInt(2, id);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        if (sqlite3_changes(db_.get()) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        return {};
    }
    std::expected<int64_t, caudio::utils::Error>
    createPlaylistFromTracks(std::string_view name, std::span<const int64_t> trackIds) {
        if (name.empty())
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        char* err = nullptr;
        internal::SqliteErrGuard guard{err};
        int rc = sqlite3_exec(db_.get(), "BEGIN IMMEDIATE", nullptr, nullptr, &err);
        if (rc != SQLITE_OK)
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Busy, err ? std::string(err) : "BEGIN failed")};
        auto pidRes = createPlaylistLocked(name);
        if (!pidRes) {
            sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            return std::unexpected{pidRes.error()};
        }
        int64_t pid = *pidRes;
        for (int64_t tid : trackIds) {
            auto r = playlistAddTrackLocked(pid, tid);
            if (!r) {
                sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
                return std::unexpected{r.error()};
            }
        }
        char* cErr = nullptr;
        internal::SqliteErrGuard cGuard{cErr};
        rc = sqlite3_exec(db_.get(), "COMMIT", nullptr, nullptr, &cErr);
        if (rc != SQLITE_OK) {
            sqlite3_exec(db_.get(), "ROLLBACK", nullptr, nullptr, nullptr);
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Internal, cErr ? std::string(cErr) : "commit failed")};
        }
        return pid;
    }
    std::expected<int64_t, caudio::utils::Error> createPlaylistLocked(std::string_view name,
                                                                      int type = 0,
                                                                      std::string_view smart_query = {}) {
        if (name.empty())
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        SqliteStatement st;
        if (auto e = st.prepare(db_.get(),
                                "INSERT INTO playlists (name, type, smart_query) VALUES (?,?,?)");
            !e)
            return std::unexpected{e.error()};
        st.bindText(1, name);
        st.bindInt(2, type);
        if (smart_query.empty())
            st.bindNull(3);
        else
            st.bindText(3, smart_query);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        return sqlite3_last_insert_rowid(db_.get());
    }
    std::expected<void, caudio::utils::Error> playlistAddTrackLocked(int64_t pid, int64_t tid,
                                                                     int64_t pos = -1) {
        if (pid == 0 || tid == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        if (pos < 0) {
            SqliteStatement ms;
            if (auto e = ms.prepare(
                    db_.get(),
                    "SELECT COALESCE(MAX(position), -1)+1 FROM playlist_items WHERE playlist_id=?");
                e) {
                ms.bindInt(1, pid);
                if (ms.step())
                    pos = ms.columnInt(0);
            }
            if (pos < 0)
                pos = 0;
        } else {
            SqliteStatement ss;
            if (auto e =
                    ss.prepare(db_.get(), "UPDATE playlist_items SET position=position+1 WHERE "
                                          "playlist_id=? AND position>=?");
                e) {
                ss.bindInt(1, pid);
                ss.bindInt(2, pos);
                (void)ss.stepDone();
            }
        }
        SqliteStatement st;
        if (auto e = st.prepare(
                db_.get(),
                "INSERT INTO playlist_items (playlist_id, track_id, position) VALUES (?,?,?)");
            !e)
            return std::unexpected{e.error()};
        st.bindInt(1, pid);
        st.bindInt(2, tid);
        st.bindInt(3, pos);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        return {};
    }
    std::expected<std::vector<Playlist>, caudio::utils::Error> listPlaylists() {
        std::shared_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        SqliteStatement st;
        if (auto e = st.prepare(db_.get(), "SELECT id, name, type, smart_query, created, modified, "
                                           "library_id FROM playlists ORDER BY id");
            !e)
            return std::unexpected{e.error()};
        std::vector<Playlist> out;
        while (st.step()) {
            Playlist p;
            p.id = st.columnInt(0);
            p.name = st.columnText(1);
            p.type = (int)st.columnInt(2);
            p.smart_query = st.columnText(3);
            p.created = st.columnInt(4);
            p.modified = st.columnInt(5);
            p.library_id = st.columnInt(6);
            out.push_back(std::move(p));
        }
        return out;
    }
    std::expected<void, caudio::utils::Error> playlistAddTrack(int64_t pid, int64_t tid,
                                                               int64_t pos = -1) {
        if (pid == 0 || tid == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        if (pos < 0) {
            SqliteStatement ms;
            if (auto e = ms.prepare(
                    db_.get(),
                    "SELECT COALESCE(MAX(position), -1)+1 FROM playlist_items WHERE playlist_id=?");
                e) {
                ms.bindInt(1, pid);
                if (ms.step())
                    pos = ms.columnInt(0);
            }
            if (pos < 0)
                pos = 0;
        } else {
            SqliteStatement ss;
            if (auto e =
                    ss.prepare(db_.get(), "UPDATE playlist_items SET position=position+1 WHERE "
                                          "playlist_id=? AND position>=?");
                e) {
                ss.bindInt(1, pid);
                ss.bindInt(2, pos);
                (void)ss.stepDone();
            }
        }
        SqliteStatement st;
        if (auto e = st.prepare(
                db_.get(),
                "INSERT INTO playlist_items (playlist_id, track_id, position) VALUES (?,?,?)");
            !e)
            return std::unexpected{e.error()};
        st.bindInt(1, pid);
        st.bindInt(2, tid);
        st.bindInt(3, pos);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        return {};
    }
    std::expected<void, caudio::utils::Error> playlistRemoveTrack(int64_t pid, int64_t tid) {
        if (pid == 0 || tid == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        SqliteStatement st;
        if (auto e = st.prepare(db_.get(),
                                "DELETE FROM playlist_items WHERE playlist_id=? AND track_id=?");
            !e)
            return std::unexpected{e.error()};
        st.bindInt(1, pid);
        st.bindInt(2, tid);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        if (sqlite3_changes(db_.get()) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        return {};
    }
    std::expected<void, caudio::utils::Error> playlistReorder(int64_t pid, int64_t from,
                                                              int64_t to) {
        if (pid == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        if (from == to)
            return {};
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        SqliteStatement sel;
        if (auto e = sel.prepare(
                db_.get(),
                "SELECT track_id FROM playlist_items WHERE playlist_id=? AND position=?");
            !e)
            return std::unexpected{e.error()};
        sel.bindInt(1, pid);
        sel.bindInt(2, from);
        if (!sel.step())
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        int64_t track_id = sel.columnInt(0);
        if (from < to) {
            SqliteStatement ss;
            if (auto e =
                    ss.prepare(db_.get(), "UPDATE playlist_items SET position=position-1 WHERE "
                                          "playlist_id=? AND position>? AND position<=?");
                e) {
                ss.bindInt(1, pid);
                ss.bindInt(2, from);
                ss.bindInt(3, to);
                (void)ss.stepDone();
            }
        } else {
            SqliteStatement ss;
            if (auto e =
                    ss.prepare(db_.get(), "UPDATE playlist_items SET position=position+1 WHERE "
                                          "playlist_id=? AND position>=? AND position<?");
                e) {
                ss.bindInt(1, pid);
                ss.bindInt(2, to);
                ss.bindInt(3, from);
                (void)ss.stepDone();
            }
        }
        SqliteStatement us;
        if (auto e = us.prepare(
                db_.get(),
                "UPDATE playlist_items SET position=? WHERE playlist_id=? AND track_id=?");
            e) {
            us.bindInt(1, to);
            us.bindInt(2, pid);
            us.bindInt(3, track_id);
            (void)us.stepDone();
        }
        return {};
    }
    std::expected<std::vector<Track>, caudio::utils::Error> playlistGetTracks(int64_t pid) {
        if (pid == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        std::shared_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        std::string sql = std::string(internal::kSelectTracksCols) +
                          " JOIN playlist_items pi ON pi.track_id=tracks.id "
                          "WHERE pi.playlist_id=? ORDER BY pi.position";
        SqliteStatement st;
        if (auto e = st.prepare(db_.get(), sql); !e)
            return std::unexpected{e.error()};
        st.bindInt(1, pid);
        std::vector<Track> out;
        while (st.step()) {
            Track t;
            internal::fillTrackFromStmt(st.get(), t);
            out.push_back(std::move(t));
        }
        return out;
    }

    // Queue (forwarded to queue partition)
    std::expected<void, caudio::utils::Error> queueEnqueue(int64_t qid, int64_t tid,
                                                           int64_t pos = -1) {
        std::unique_lock lk{dbMutex_};
        return queueEnqueueLocked(qid, tid, pos);
    }

    std::expected<void, caudio::utils::Error> queueEnqueueLocked(int64_t qid, int64_t tid,
                                                                 int64_t pos = -1) {
        return caudio::db::queueEnqueueLocked(db_.get(), qid, tid, pos);
    }

    std::expected<QueueItem, caudio::utils::Error> queueDequeue(int64_t qid) {
        std::unique_lock lk{dbMutex_};
        return queueDequeueLocked(qid);
    }

    std::expected<QueueItem, caudio::utils::Error> queueDequeueLocked(int64_t qid) {
        return caudio::db::queueDequeueLocked(db_.get(), qid);
    }

    std::expected<QueueItem, caudio::utils::Error> queuePeekLocked(int64_t qid) {
        return caudio::db::queuePeekLocked(db_.get(), qid);
    }

    std::expected<QueueItem, caudio::utils::Error> queuePeek(int64_t qid) {
        if (qid == 0)
            qid = 1;
        std::shared_lock lk{dbMutex_};
        return caudio::db::queuePeekLocked(db_.get(), qid);
    }

    std::expected<void, caudio::utils::Error> queueRemove(int64_t qid, int64_t pos) {
        if (qid == 0)
            qid = 1;
        std::unique_lock lk{dbMutex_};
        return caudio::db::queueRemoveLocked(db_.get(), qid, pos);
    }

    std::expected<void, caudio::utils::Error> queueClear(int64_t qid) {
        if (qid == 0)
            qid = 1;
        std::unique_lock lk{dbMutex_};
        return caudio::db::queueClearLocked(db_.get(), qid);
    }

    std::expected<std::vector<QueueItem>, caudio::utils::Error> queueList(int64_t qid) {
        if (qid == 0)
            qid = 1;
        std::shared_lock lk{dbMutex_};
        return caudio::db::queueListLocked(db_.get(), qid);
    }

    std::expected<std::vector<QueueItem>, caudio::utils::Error> getQueueItems(int64_t qid) {
        if (qid == 0)
            qid = 1;
        std::shared_lock lk{dbMutex_};
        return caudio::db::getQueueItemsLocked(db_.get(), qid);
    }

    std::expected<Queue, caudio::utils::Error> getQueue(int64_t qid) {
        if (qid == 0)
            qid = 1;
        std::shared_lock lk{dbMutex_};
        return caudio::db::getQueueLocked(db_.get(), qid);
    }

    std::expected<std::vector<Queue>, caudio::utils::Error> listQueues() {
        std::shared_lock lk{dbMutex_};
        return caudio::db::listQueuesLocked(db_.get());
    }

    std::expected<int64_t, caudio::utils::Error> createQueue(std::string_view name,
                                                             int64_t library_id = 1) {
        if (name.empty())
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        std::unique_lock lk{dbMutex_};
        return caudio::db::createQueueLocked(db_.get(), name, library_id);
    }

    std::expected<void, caudio::utils::Error> deleteQueue(int64_t qid) {
        if (qid == 0)
            qid = 1;
        std::unique_lock lk{dbMutex_};
        return caudio::db::deleteQueueLocked(db_.get(), qid);
    }

    std::expected<void, caudio::utils::Error> setQueueRepeat(int64_t qid, int repeat_mode) {
        if (qid == 0)
            qid = 1;
        std::unique_lock lk{dbMutex_};
        return caudio::db::setQueueRepeatLocked(db_.get(), qid, repeat_mode);
    }

    size_t queueCountLocked(int64_t qid) {
        if (qid == 0)
            qid = 1;
        std::shared_lock lk{dbMutex_};
        return caudio::db::queueCountLocked(db_.get(), qid);
    }

    // History
    std::expected<void, caudio::utils::Error> historyAdd(const HistoryEntry& e) {
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        std::string_view sql = "INSERT INTO history (track_id, started_at, completed_at, "
                               "position_ms, completion_pct, queue_id) VALUES (?,?,?,?,?,?)";
        std::unique_lock cacheLk{cacheMutex_};
        auto sRes = getCachedForUse(sql);
        if (!sRes)
            return std::unexpected{sRes.error()};
        SqliteStatement& st = *(*sRes);
        st.bindInt(1, e.track_id);
        st.bindInt(2, e.started_at);
        if (e.completed_at)
            st.bindInt(3, e.completed_at);
        else
            st.bindNull(3);
        st.bindInt(4, e.position_ms);
        st.bindDouble(5, e.completion_pct);
        st.bindInt(6, e.queue_id ? e.queue_id : 1);
        int rc = st.stepDone();
        st.reset();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        return {};
    }
    std::expected<std::vector<HistoryEntry>, caudio::utils::Error>
    historyList(const HistoryQuery* q = nullptr) {
        std::shared_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        std::string sql = "SELECT id, track_id, started_at, completed_at, position_ms, "
                          "completion_pct, queue_id FROM history WHERE 1=1";
        if (q && q->track_id)
            sql += " AND track_id=?";
        if (q && q->queue_id)
            sql += " AND queue_id=?";
        sql += " ORDER BY started_at DESC";
        bool has_lim = q && q->limit > 0;
        bool has_off = q && q->offset > 0;
        if (has_lim) {
            sql += " LIMIT ?";
            if (has_off)
                sql += " OFFSET ?";
        } else if (has_off)
            sql += " LIMIT -1 OFFSET ?";
        SqliteStatement st;
        if (auto e = st.prepare(db_.get(), sql); !e)
            return std::unexpected{e.error()};
        int idx = 1;
        if (q) {
            if (q->track_id)
                st.bindInt(idx++, *q->track_id);
            if (q->queue_id)
                st.bindInt(idx++, *q->queue_id);
            if (has_lim) {
                st.bindInt(idx++, q->limit);
                if (has_off)
                    st.bindInt(idx++, q->offset);
            } else if (has_off)
                st.bindInt(idx++, q->offset);
        }
        std::vector<HistoryEntry> out;
        while (st.step()) {
            HistoryEntry e;
            e.id = st.columnInt(0);
            e.track_id = st.columnInt(1);
            e.started_at = st.columnInt(2);
            e.completed_at = st.columnInt(3);
            e.position_ms = st.columnInt(4);
            e.completion_pct = st.columnDouble(5);
            e.queue_id = st.columnInt(6);
            out.push_back(e);
        }
        return out;
    }

    // Bookmarks
    std::expected<void, caudio::utils::Error> bookmarkAdd(int64_t tid, int64_t pos,
                                                          std::string_view note = {}) {
        if (tid == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        SqliteStatement st;
        if (auto e = st.prepare(
                db_.get(), "INSERT INTO bookmarks (track_id, position_ms, note) VALUES (?,?,?)");
            !e)
            return std::unexpected{e.error()};
        st.bindInt(1, tid);
        st.bindInt(2, pos);
        if (note.empty())
            st.bindNull(3);
        else
            st.bindText(3, note);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        return {};
    }
    std::expected<std::vector<Bookmark>, caudio::utils::Error> bookmarkList(int64_t tid) {
        if (tid == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        std::shared_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        SqliteStatement st;
        if (auto e = st.prepare(db_.get(), "SELECT id, track_id, position_ms, note, created FROM "
                                           "bookmarks WHERE track_id=? ORDER BY created");
            !e)
            return std::unexpected{e.error()};
        st.bindInt(1, tid);
        std::vector<Bookmark> out;
        while (st.step()) {
            Bookmark b;
            b.id = st.columnInt(0);
            b.track_id = st.columnInt(1);
            b.position_ms = st.columnInt(2);
            b.note = st.columnText(3);
            b.created = st.columnInt(4);
            out.push_back(std::move(b));
        }
        return out;
    }

    // Libraries
    std::expected<int64_t, caudio::utils::Error> libraryAdd(std::string_view path,
                                                            std::string_view name = {}) {
        if (path.empty())
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        SqliteStatement st;
        if (auto e = st.prepare(db_.get(), "INSERT INTO libraries (path, name) VALUES (?,?)"); !e)
            return std::unexpected{e.error()};
        st.bindText(1, path);
        if (name.empty())
            st.bindNull(2);
        else
            st.bindText(2, name);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        return sqlite3_last_insert_rowid(db_.get());
    }
    std::expected<std::vector<Library>, caudio::utils::Error> libraryList() {
        std::shared_lock lk{dbMutex_};
        return libraryListLocked();
    }
    std::expected<void, caudio::utils::Error> libraryUpdate(const Library& l) {
        std::unique_lock lk{dbMutex_};
        return libraryUpdateLocked(l);
    }
    std::expected<void, caudio::utils::Error> libraryDelete(int64_t id) {
        if (id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
        std::unique_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        SqliteStatement st;
        if (auto e = st.prepare(db_.get(), "DELETE FROM libraries WHERE id=?"); !e)
            return std::unexpected{e.error()};
        st.bindInt(1, id);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Internal,
                                                            sqlite3_errmsg(db_.get()))};
        if (sqlite3_changes(db_.get()) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
        return {};
    }

    std::expected<DbStats, caudio::utils::Error> getStats() {
        std::shared_lock lk{dbMutex_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
        DbStats s;
        std::unique_lock cacheLk{cacheMutex_};
        auto one = [&](std::string_view sql, int64_t& out) {
            auto sRes = getCachedForUse(sql);
            if (!sRes)
                return;
            SqliteStatement& st = *(*sRes);
            if (st.step())
                out = st.columnInt(0);
            st.reset();
        };
        one("SELECT COUNT(*) FROM tracks WHERE deleted_at IS NULL", s.num_tracks);
        one("SELECT COUNT(*) FROM playlists", s.num_playlists);
        one("SELECT COUNT(*) FROM queue", s.num_queue_items);
        one("SELECT COUNT(*) FROM history", s.num_history);
        one("SELECT COUNT(*) FROM bookmarks", s.num_bookmarks);
        one("SELECT COUNT(*) FROM libraries", s.num_libraries);
        if (auto sRes = getCachedForUse(
                "SELECT COALESCE(SUM(duration),0) FROM tracks WHERE deleted_at IS NULL");
            sRes) {
            SqliteStatement& st = *(*sRes);
            if (st.step())
                s.total_duration_ms = (int64_t)(st.columnDouble(0) * 1000);
            st.reset();
        }
        return s;
    }

    // insert overload for legacy database.cppm signature
    std::expected<void, caudio::utils::Error> insertTrackLegacy(int64_t libraryId,
                                                                std::string_view name,
                                                                std::string_view path,
                                                                std::string_view fpHex = {}) {
        Track t;
        t.library_id = libraryId;
        t.title = std::string(name);
        t.path = std::string(path);
        if (!fpHex.empty()) {
            if (!internal::fromHex(fpHex, t.fingerprint)) {
                // fallback for non-hex or wrong length: raw copy
                std::memset(t.fingerprint.data(), 0, 32);
                for (size_t i = 0; i < fpHex.size() && i < 32; i++)
                    t.fingerprint[i] = (uint8_t)fpHex[i];
            }
        } else {
            // generate fingerprint from path via fnv fallback
            t.fingerprint = internal::fallbackFingerprint(t.path);
        }
        auto r = insertTrack(t);
        if (!r)
            return std::unexpected{r.error()};
        return {};
    }

}; // class Database

} // namespace caudio::db
