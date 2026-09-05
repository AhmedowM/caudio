module;
#include <cstring>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sqlite3.h>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

export module caudio.db:database;

import caudio.utils;
import :types;
import :schema;

export namespace caudio::db {

class Statement final {
  public:
    Statement() = default;
    ~Statement() {
        if (stmt_)
            sqlite3_finalize(stmt_);
    }
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;
    Statement(Statement &&o) noexcept : stmt_(o.stmt_) {
        o.stmt_ = nullptr;
    }
    Statement &operator=(Statement &&o) noexcept {
        if (this != &o) {
            if (stmt_)
                sqlite3_finalize(stmt_);
            stmt_ = o.stmt_;
            o.stmt_ = nullptr;
        }
        return *this;
    }
    [[nodiscard]] std::expected<void, caudio::utils::Error> prepare(sqlite3 *db,
                                                                    std::string_view sql) {
        if (stmt_)
            sqlite3_finalize(stmt_);
        stmt_ = nullptr;
        int rc = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr);
        if (rc != SQLITE_OK) {
            std::string msg = db ? sqlite3_errmsg(db) : "prepare failed";
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, msg)};
        }
        return {};
    }
    void bindInt(int idx, int64_t v) {
        if (stmt_)
            sqlite3_bind_int64(stmt_, idx, v);
    }
    void bindDouble(int idx, double v) {
        if (stmt_)
            sqlite3_bind_double(stmt_, idx, v);
    }
    void bindText(int idx, std::string_view v) {
        if (!stmt_)
            return;
        if (v.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return;
        sqlite3_bind_text(stmt_, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    }
    void bindBlob(int idx, const void *data, int n) {
        if (stmt_)
            sqlite3_bind_blob(stmt_, idx, data, n, SQLITE_TRANSIENT);
    }
    void bindNull(int idx) {
        if (stmt_)
            sqlite3_bind_null(stmt_, idx);
    }
    [[nodiscard]] bool step() {
        if (!stmt_)
            return false;
        int rc = sqlite3_step(stmt_);
        return rc == SQLITE_ROW;
    }
    [[nodiscard]] int stepDone() {
        if (!stmt_)
            return SQLITE_ERROR;
        return sqlite3_step(stmt_);
    }
    int64_t columnInt(int idx) const {
        return stmt_ ? sqlite3_column_int64(stmt_, idx) : 0;
    }
    double columnDouble(int idx) const {
        return stmt_ ? sqlite3_column_double(stmt_, idx) : 0.0;
    }
    std::string columnText(int idx) const {
        if (!stmt_)
            return {};
        const char *v = reinterpret_cast<const char *>(sqlite3_column_text(stmt_, idx));
        return v ? std::string(v) : std::string{};
    }
    const void *columnBlob(int idx, int &n) const {
        if (!stmt_) {
            n = 0;
            return nullptr;
        }
        n = sqlite3_column_bytes(stmt_, idx);
        return sqlite3_column_blob(stmt_, idx);
    }
    void reset() {
        if (stmt_)
            sqlite3_reset(stmt_);
    }
    sqlite3_stmt *get() const {
        return stmt_;
    }
    [[nodiscard]] explicit operator bool() const {
        return stmt_ != nullptr;
    }

  private:
    sqlite3_stmt *stmt_{nullptr};
};

class Transaction final {
  public:
    explicit Transaction(sqlite3 *db) : db_(db) {
        if (db_) {
            char *err = nullptr;
            int rc = sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, &err);
            if (rc != SQLITE_OK) {
                if (err)
                    sqlite3_free(err);
                db_ = nullptr;
            } else {
                active_ = true;
            }
        }
    }
    ~Transaction() {
        if (active_ && db_)
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    Transaction(Transaction &&o) noexcept : db_(o.db_), active_(o.active_) {
        o.db_ = nullptr;
        o.active_ = false;
    }
    Transaction &operator=(Transaction &&o) noexcept {
        if (this != &o) {
            if (active_ && db_)
                sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            db_ = o.db_;
            active_ = o.active_;
            o.db_ = nullptr;
            o.active_ = false;
        }
        return *this;
    }
    [[nodiscard]] caudio::utils::Error commit() {
        if (!db_ || !active_)
            return caudio::utils::makeError(caudio::utils::Result::Internal, "no transaction");
        char *err = nullptr;
        int rc = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "commit failed";
            if (err)
                sqlite3_free(err);
            return caudio::utils::makeError(caudio::utils::Result::Internal, msg);
        }
        active_ = false;
        db_ = nullptr;
        return caudio::utils::Error{};
    }
    bool active() const {
        return active_;
    }

  private:
    sqlite3 *db_{nullptr};
    bool active_{false};
};

// forward decl helpers
inline void fillTrackFromStmt(sqlite3_stmt *s, Track &out) {
    out.id = sqlite3_column_int64(s, 0);
    int n = 0;
    const void *fp = sqlite3_column_blob(s, 1);
    n = sqlite3_column_bytes(s, 1);
    out.fingerprint.fill(0);
    if (fp && n == 32)
        std::memcpy(out.fingerprint.data(), fp, 32);
    else if (fp && n > 0)
        std::memcpy(out.fingerprint.data(), fp, n > 32 ? 32 : n);
    auto txt = [&](int c) {
        const unsigned char *p = sqlite3_column_text(s, c);
        return p ? std::string((const char *)p) : std::string{};
    };
    out.path = txt(2);
    out.deleted_at = sqlite3_column_int64(s, 3);
    out.size = sqlite3_column_int64(s, 4);
    out.mtime = sqlite3_column_int64(s, 5);
    out.duration = sqlite3_column_double(s, 6);
    out.sample_rate = (uint32_t)sqlite3_column_int(s, 7);
    out.channels = (uint32_t)sqlite3_column_int(s, 8);
    out.bitrate = sqlite3_column_int(s, 9);
    out.title = txt(10);
    out.artist = txt(11);
    out.album = txt(12);
    out.albumArtist = txt(13);
    out.genre = txt(14);
    out.year = sqlite3_column_int(s, 15);
    out.track_num = sqlite3_column_int(s, 16);
    out.disc_num = sqlite3_column_int(s, 17);
    out.cover_art_path = txt(18);
    out.rating = sqlite3_column_int(s, 19);
    out.play_count = sqlite3_column_int64(s, 20);
    out.last_played = sqlite3_column_int64(s, 21);
    out.date_added = sqlite3_column_int64(s, 22);
    out.last_scanned = sqlite3_column_int64(s, 23);
    out.dirty = sqlite3_column_int(s, 24);
    out.library_id = sqlite3_column_int64(s, 25);
}

inline constexpr std::string_view kSelectTracksCols =
    "SELECT id, fingerprint, path, deleted_at, size, mtime, duration, sample_rate, channels, "
    "bitrate, title, artist, album, album_artist, genre, year, track_num, disc_num, "
    "cover_art_path, rating, play_count, last_played, date_added, last_scanned, dirty, library_id "
    "FROM tracks";

class Database final {
  public:
    Database() = default;
    ~Database() {
        if (db_)
            sqlite3_close(db_);
    }
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    Database(Database &&o) noexcept : db_(o.db_) {
        o.db_ = nullptr;
    }
    Database &operator=(Database &&o) noexcept {
        if (this != &o) {
            if (db_)
                sqlite3_close(db_);
            db_ = o.db_;
            o.db_ = nullptr;
        }
        return *this;
    }
    static std::expected<std::unique_ptr<Database>, caudio::utils::Error>
    open(std::string_view path) {
        std::string dbPath = path.empty() ? ":memory:" : std::string(path);
        sqlite3 *raw = nullptr;
        int rc =
            sqlite3_open_v2(dbPath.c_str(), &raw,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI, nullptr);
        if (rc != SQLITE_OK) {
            std::string msg = raw ? sqlite3_errmsg(raw) : "open failed";
            if (raw)
                sqlite3_close(raw);
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, msg)};
        }
        char *err = nullptr;
        rc = sqlite3_exec(raw, std::string(kSchema).c_str(), nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? std::string(err) : sqlite3_errmsg(raw);
            if (err)
                sqlite3_free(err);
            sqlite3_close(raw);
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::Result::Corrupt, std::string("schema init failed: ") + msg)};
        }
        rc = sqlite3_exec(raw, std::string(kSchemaDefaultLibrary).c_str(), nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? std::string(err) : sqlite3_errmsg(raw);
            if (err)
                sqlite3_free(err);
            // not fatal? but log
        }
        if (err)
            sqlite3_free(err);
        auto db = std::make_unique<Database>();
        db->db_ = raw;
        return db;
    }
    sqlite3 *handle() const {
        return db_;
    }
    std::shared_mutex &mutex() const {
        return m_;
    }

    // Track CRUD
    std::expected<int64_t, caudio::utils::Error> insertTrack(const Track &t) {
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        const char *sql =
            "INSERT INTO tracks (fingerprint, path, size, mtime, duration, sample_rate, channels, "
            "bitrate, title, artist, album, album_artist, genre, year, track_num, disc_num, "
            "cover_art_path, rating, play_count, last_played, date_added, last_scanned, dirty, "
            "library_id) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
        Statement st;
        if (auto e = st.prepare(db_, sql); !e)
            return std::unexpected{e.error()};
        st.bindBlob(1, t.fingerprint.data(), 32);
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
        st.bindText(12, t.albumArtist);
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
        st.bindInt(23, t.dirty);
        st.bindInt(24, t.library_id ? t.library_id : 1);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE) {
            int ec = sqlite3_extended_errcode(db_);
            if (ec == SQLITE_CONSTRAINT_UNIQUE || rc == SQLITE_CONSTRAINT)
                return std::unexpected{caudio::utils::makeError(
                    caudio::utils::Result::AlreadyExists, sqlite3_errmsg(db_))};
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        }
        return sqlite3_last_insert_rowid(db_);
    }
    std::expected<void, caudio::utils::Error> updateTrack(const Track &t) {
        if (t.id == 0)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::InvalidArg, "id 0")};
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        const char *sql =
            "UPDATE tracks SET fingerprint=?, path=?, size=?, mtime=?, duration=?, sample_rate=?, "
            "channels=?, bitrate=?, title=?, artist=?, album=?, album_artist=?, genre=?, year=?, "
            "track_num=?, disc_num=?, cover_art_path=?, rating=?, play_count=?, last_played=?, "
            "date_added=?, last_scanned=?, dirty=?, library_id=?, deleted_at=? WHERE id=?";
        Statement st;
        if (auto e = st.prepare(db_, sql); !e)
            return std::unexpected{e.error()};
        st.bindBlob(1, t.fingerprint.data(), 32);
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
        st.bindText(12, t.albumArtist);
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
        st.bindInt(23, t.dirty);
        st.bindInt(24, t.library_id ? t.library_id : 1);
        if (t.deleted_at)
            st.bindInt(25, t.deleted_at);
        else
            st.bindNull(25);
        st.bindInt(26, t.id);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        if (sqlite3_changes(db_) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        return {};
    }
    std::expected<void, caudio::utils::Error> deleteTrack(int64_t id) {
        if (id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(db_, "DELETE FROM tracks WHERE id=?"); !e)
            return std::unexpected{e.error()};
        st.bindInt(1, id);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        if (sqlite3_changes(db_) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        return {};
    }
    std::expected<Track, caudio::utils::Error> getTrack(int64_t id) {
        if (id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        std::shared_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        std::string sql = std::string(kSelectTracksCols) + " WHERE id=?";
        Statement st;
        if (auto e = st.prepare(db_, sql); !e)
            return std::unexpected{e.error()};
        st.bindInt(1, id);
        if (!st.step())
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        Track t;
        fillTrackFromStmt(st.get(), t);
        return t;
    }
    std::expected<Track, caudio::utils::Error>
    findByFingerprint(const std::array<uint8_t, 32> &fp) {
        std::shared_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        std::string sql = std::string(kSelectTracksCols) + " WHERE fingerprint=?";
        Statement st;
        if (auto e = st.prepare(db_, sql); !e)
            return std::unexpected{e.error()};
        st.bindBlob(1, fp.data(), 32);
        if (!st.step())
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        Track t;
        fillTrackFromStmt(st.get(), t);
        return t;
    }
    std::expected<Track, caudio::utils::Error> findByPath(std::string_view path) {
        std::shared_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        std::string sql = std::string(kSelectTracksCols) + " WHERE path=?";
        Statement st;
        if (auto e = st.prepare(db_, sql); !e)
            return std::unexpected{e.error()};
        st.bindText(1, path);
        if (!st.step())
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        Track t;
        fillTrackFromStmt(st.get(), t);
        return t;
    }
    std::expected<std::vector<Track>, caudio::utils::Error>
    listTracks(const TrackQuery *q = nullptr) {
        std::shared_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        std::string sql = std::string(kSelectTracksCols) + " WHERE 1=1";
        if (q) {
            if (q->has_library_id)
                sql += " AND library_id=?";
            if (!q->artist.empty())
                sql += " AND artist=?";
            if (!q->album.empty())
                sql += " AND album=?";
            if (!q->genre.empty())
                sql += " AND genre=?";
            if (q->has_year)
                sql += " AND year=?";
            if (q->has_dirty)
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
        Statement st;
        if (auto e = st.prepare(db_, sql); !e)
            return std::unexpected{e.error()};
        auto escapeLike = [](std::string_view s) {
            std::string o;
            o.reserve(s.size() * 2);
            for (char c : s) {
                if (c == '%' || c == '_' || c == '\\')
                    o.push_back('\\');
                o.push_back(c);
            }
            return o;
        };
        int idx = 1;
        std::string likePat;
        if (q) {
            if (q->has_library_id)
                st.bindInt(idx++, q->library_id);
            if (!q->artist.empty())
                st.bindText(idx++, q->artist);
            if (!q->album.empty())
                st.bindText(idx++, q->album);
            if (!q->genre.empty())
                st.bindText(idx++, q->genre);
            if (q->has_year)
                st.bindInt(idx++, q->year);
            if (q->has_dirty)
                st.bindInt(idx++, q->dirty);
            if (!q->search.empty()) {
                std::string esc = escapeLike(q->search);
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
            fillTrackFromStmt(st.get(), t);
            out.push_back(std::move(t));
        }
        return out;
    }
    std::expected<void, caudio::utils::Error> setDirty(int64_t id, int dirty) {
        if (id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(db_, "UPDATE tracks SET dirty=? WHERE id=?"); !e)
            return std::unexpected{e.error()};
        st.bindInt(1, dirty);
        st.bindInt(2, id);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        if (sqlite3_changes(db_) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        return {};
    }

    // compat helpers for older database.cppm API
    std::expected<std::string, caudio::utils::Error> getTrackName(int64_t id) {
        auto r = getTrack(id);
        if (!r)
            return std::unexpected{r.error()};
        return r->title.empty() ? r->path : r->title;
    }
    std::expected<std::vector<std::tuple<int64_t, std::string>>, caudio::utils::Error>
    listTracksSimple(int64_t libraryId = 0) {
        TrackQuery q;
        if (libraryId > 0) {
            q.has_library_id = true;
            q.library_id = libraryId;
        }
        auto r = listTracks(&q);
        if (!r)
            return std::unexpected{r.error()};
        std::vector<std::tuple<int64_t, std::string>> out;
        for (auto &t : *r)
            out.emplace_back(t.id, t.title.empty() ? t.path : t.title);
        return out;
    }

    // Playlists
    std::expected<int64_t, caudio::utils::Error> createPlaylist(std::string_view name, int type = 0,
                                                                std::string_view smart_query = {}) {
        if (name.empty())
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e =
                st.prepare(db_, "INSERT INTO playlists (name, type, smart_query) VALUES (?,?,?)");
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
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        return sqlite3_last_insert_rowid(db_);
    }
    std::expected<Playlist, caudio::utils::Error> getPlaylist(int64_t id) {
        if (id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        std::shared_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(db_, "SELECT id, name, type, smart_query, created, modified, "
                                     "library_id FROM playlists WHERE id=?");
            !e)
            return std::unexpected{e.error()};
        st.bindInt(1, id);
        if (!st.step())
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
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
    std::expected<void, caudio::utils::Error> updatePlaylist(const Playlist &p) {
        if (p.id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(db_, "UPDATE playlists SET name=?, type=?, smart_query=?, "
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
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        if (sqlite3_changes(db_) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        return {};
    }
    std::expected<void, caudio::utils::Error> deletePlaylist(int64_t id) {
        if (id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(db_, "DELETE FROM playlists WHERE id=?"); !e)
            return std::unexpected{e.error()};
        st.bindInt(1, id);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        if (sqlite3_changes(db_) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        return {};
    }
    std::expected<std::vector<Playlist>, caudio::utils::Error> listPlaylists() {
        std::shared_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(db_, "SELECT id, name, type, smart_query, created, modified, "
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
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        if (pos < 0) {
            Statement ms;
            if (auto e = ms.prepare(
                    db_,
                    "SELECT COALESCE(MAX(position), -1)+1 FROM playlist_items WHERE playlist_id=?");
                e) {
                ms.bindInt(1, pid);
                if (ms.step())
                    pos = ms.columnInt(0);
            }
            if (pos < 0)
                pos = 0;
        } else {
            Statement ss;
            if (auto e = ss.prepare(db_, "UPDATE playlist_items SET position=position+1 WHERE "
                                         "playlist_id=? AND position>=?");
                e) {
                ss.bindInt(1, pid);
                ss.bindInt(2, pos);
                (void)ss.stepDone();
            }
        }
        Statement st;
        if (auto e = st.prepare(
                db_, "INSERT INTO playlist_items (playlist_id, track_id, position) VALUES (?,?,?)");
            !e)
            return std::unexpected{e.error()};
        st.bindInt(1, pid);
        st.bindInt(2, tid);
        st.bindInt(3, pos);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        return {};
    }
    std::expected<void, caudio::utils::Error> playlistRemoveTrack(int64_t pid, int64_t tid) {
        if (pid == 0 || tid == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e =
                st.prepare(db_, "DELETE FROM playlist_items WHERE playlist_id=? AND track_id=?");
            !e)
            return std::unexpected{e.error()};
        st.bindInt(1, pid);
        st.bindInt(2, tid);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        if (sqlite3_changes(db_) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        return {};
    }
    std::expected<void, caudio::utils::Error> playlistReorder(int64_t pid, int64_t from,
                                                              int64_t to) {
        if (pid == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        if (from == to)
            return {};
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement sel;
        if (auto e = sel.prepare(
                db_, "SELECT track_id FROM playlist_items WHERE playlist_id=? AND position=?");
            !e)
            return std::unexpected{e.error()};
        sel.bindInt(1, pid);
        sel.bindInt(2, from);
        if (!sel.step())
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        int64_t track_id = sel.columnInt(0);
        if (from < to) {
            Statement ss;
            if (auto e = ss.prepare(db_, "UPDATE playlist_items SET position=position-1 WHERE "
                                         "playlist_id=? AND position>? AND position<=?");
                e) {
                ss.bindInt(1, pid);
                ss.bindInt(2, from);
                ss.bindInt(3, to);
                (void)ss.stepDone();
            }
        } else {
            Statement ss;
            if (auto e = ss.prepare(db_, "UPDATE playlist_items SET position=position+1 WHERE "
                                         "playlist_id=? AND position>=? AND position<?");
                e) {
                ss.bindInt(1, pid);
                ss.bindInt(2, to);
                ss.bindInt(3, from);
                (void)ss.stepDone();
            }
        }
        Statement us;
        if (auto e = us.prepare(
                db_, "UPDATE playlist_items SET position=? WHERE playlist_id=? AND track_id=?");
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
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        std::shared_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        std::string sql = std::string(kSelectTracksCols) +
                          " JOIN playlist_items pi ON pi.track_id=tracks.id "
                          "WHERE pi.playlist_id=? ORDER BY pi.position";
        Statement st;
        if (auto e = st.prepare(db_, sql); !e)
            return std::unexpected{e.error()};
        st.bindInt(1, pid);
        std::vector<Track> out;
        while (st.step()) {
            Track t;
            fillTrackFromStmt(st.get(), t);
            out.push_back(std::move(t));
        }
        return out;
    }

    // Queue
    std::expected<void, caudio::utils::Error> queueEnqueue(int64_t qid, int64_t tid,
                                                           int64_t pos = -1) {
        if (tid == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        if (qid == 0)
            qid = 1;
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        if (pos < 0) {
            Statement ms;
            if (auto e = ms.prepare(
                    db_, "SELECT COALESCE(MAX(position), -1)+1 FROM queue WHERE queue_id=?");
                e) {
                ms.bindInt(1, qid);
                if (ms.step())
                    pos = ms.columnInt(0);
            }
            if (pos < 0)
                pos = 0;
        } else {
            Statement ss;
            if (auto e = ss.prepare(
                    db_, "UPDATE queue SET position=position+1 WHERE queue_id=? AND position>=?");
                e) {
                ss.bindInt(1, qid);
                ss.bindInt(2, pos);
                (void)ss.stepDone();
            }
        }
        Statement st;
        if (auto e =
                st.prepare(db_, "INSERT INTO queue (queue_id, track_id, position) VALUES (?,?,?)");
            !e)
            return std::unexpected{e.error()};
        st.bindInt(1, qid);
        st.bindInt(2, tid);
        st.bindInt(3, pos);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        return {};
    }
    std::expected<QueueItem, caudio::utils::Error> queueDequeue(int64_t qid) {
        if (qid == 0)
            qid = 1;
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(db_, "SELECT id, queue_id, track_id, position, added FROM queue "
                                     "WHERE queue_id=? ORDER BY position LIMIT 1");
            !e)
            return std::unexpected{e.error()};
        st.bindInt(1, qid);
        if (!st.step())
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        QueueItem it;
        it.id = st.columnInt(0);
        it.queue_id = st.columnInt(1);
        it.trackId = st.columnInt(2);
        it.position = st.columnInt(3);
        it.added = st.columnInt(4);
        Statement del;
        if (auto e = del.prepare(db_, "DELETE FROM queue WHERE id=?"); e) {
            del.bindInt(1, it.id);
            (void)del.stepDone();
        }
        Statement sh;
        if (auto e = sh.prepare(
                db_, "UPDATE queue SET position=position-1 WHERE queue_id=? AND position>?");
            e) {
            sh.bindInt(1, qid);
            sh.bindInt(2, it.position);
            (void)sh.stepDone();
        }
        return it;
    }
    std::expected<void, caudio::utils::Error> queueRemove(int64_t qid, int64_t pos) {
        if (qid == 0)
            qid = 1;
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(db_, "DELETE FROM queue WHERE queue_id=? AND position=?"); !e)
            return std::unexpected{e.error()};
        st.bindInt(1, qid);
        st.bindInt(2, pos);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        if (sqlite3_changes(db_) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        Statement sh;
        if (auto e = sh.prepare(
                db_, "UPDATE queue SET position=position-1 WHERE queue_id=? AND position>?");
            e) {
            sh.bindInt(1, qid);
            sh.bindInt(2, pos);
            (void)sh.stepDone();
        }
        return {};
    }
    std::expected<void, caudio::utils::Error> queueClear(int64_t qid) {
        if (qid == 0)
            qid = 1;
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(db_, "DELETE FROM queue WHERE queue_id=?"); !e)
            return std::unexpected{e.error()};
        st.bindInt(1, qid);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        return {};
    }
    std::expected<std::vector<QueueItem>, caudio::utils::Error> queueList(int64_t qid) {
        if (qid == 0)
            qid = 1;
        std::shared_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(db_, "SELECT id, queue_id, track_id, position, added FROM queue "
                                     "WHERE queue_id=? ORDER BY position");
            !e)
            return std::unexpected{e.error()};
        st.bindInt(1, qid);
        std::vector<QueueItem> out;
        while (st.step()) {
            QueueItem it;
            it.id = st.columnInt(0);
            it.queue_id = st.columnInt(1);
            it.trackId = st.columnInt(2);
            it.position = st.columnInt(3);
            it.added = st.columnInt(4);
            out.push_back(it);
        }
        return out;
    }

    // History
    std::expected<void, caudio::utils::Error> historyAdd(const HistoryEntry &e) {
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto ee =
                st.prepare(db_, "INSERT INTO history (track_id, started_at, completed_at, "
                                "position_ms, completion_pct, queue_id) VALUES (?,?,?,?,?,?)");
            !ee)
            return std::unexpected{ee.error()};
        st.bindInt(1, e.trackId);
        st.bindInt(2, e.started_at);
        if (e.completed_at)
            st.bindInt(3, e.completed_at);
        else
            st.bindNull(3);
        st.bindInt(4, e.position_ms);
        st.bindDouble(5, e.completion_pct);
        st.bindInt(6, e.queue_id ? e.queue_id : 1);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        return {};
    }
    std::expected<std::vector<HistoryEntry>, caudio::utils::Error>
    historyList(const HistoryQuery *q = nullptr) {
        std::shared_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        std::string sql = "SELECT id, track_id, started_at, completed_at, position_ms, "
                          "completion_pct, queue_id FROM history WHERE 1=1";
        if (q && q->has_track_id)
            sql += " AND track_id=?";
        if (q && q->has_queue_id)
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
        Statement st;
        if (auto e = st.prepare(db_, sql); !e)
            return std::unexpected{e.error()};
        int idx = 1;
        if (q) {
            if (q->has_track_id)
                st.bindInt(idx++, q->track_id);
            if (q->has_queue_id)
                st.bindInt(idx++, q->queue_id);
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
            e.trackId = st.columnInt(1);
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
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(
                db_, "INSERT INTO bookmarks (track_id, position_ms, note) VALUES (?,?,?)");
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
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        return {};
    }
    std::expected<std::vector<Bookmark>, caudio::utils::Error> bookmarkList(int64_t tid) {
        if (tid == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        std::shared_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(db_, "SELECT id, track_id, position_ms, note, created FROM "
                                     "bookmarks WHERE track_id=? ORDER BY created");
            !e)
            return std::unexpected{e.error()};
        st.bindInt(1, tid);
        std::vector<Bookmark> out;
        while (st.step()) {
            Bookmark b;
            b.id = st.columnInt(0);
            b.trackId = st.columnInt(1);
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
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(db_, "INSERT INTO libraries (path, name) VALUES (?,?)"); !e)
            return std::unexpected{e.error()};
        st.bindText(1, path);
        if (name.empty())
            st.bindNull(2);
        else
            st.bindText(2, name);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        return sqlite3_last_insert_rowid(db_);
    }
    std::expected<std::vector<Library>, caudio::utils::Error> libraryList() {
        std::shared_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(db_, "SELECT id, path, name, date_added, last_scanned, auto_scan, "
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
            l.auto_scan = (int)st.columnInt(5);
            l.recursive = (int)st.columnInt(6);
            l.extensions = st.columnText(7);
            out.push_back(std::move(l));
        }
        return out;
    }
    std::expected<void, caudio::utils::Error> libraryUpdate(const Library &l) {
        if (l.id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(db_, "UPDATE libraries SET path=?, name=?, last_scanned=?, "
                                     "auto_scan=?, recursive=?, extensions=? WHERE id=?");
            !e)
            return std::unexpected{e.error()};
        st.bindText(1, l.path);
        if (l.name.empty())
            st.bindNull(2);
        else
            st.bindText(2, l.name);
        st.bindInt(3, l.last_scanned);
        st.bindInt(4, l.auto_scan);
        st.bindInt(5, l.recursive);
        st.bindText(6, l.extensions);
        st.bindInt(7, l.id);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        if (sqlite3_changes(db_) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        return {};
    }
    std::expected<void, caudio::utils::Error> libraryDelete(int64_t id) {
        if (id == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
        std::unique_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        Statement st;
        if (auto e = st.prepare(db_, "DELETE FROM libraries WHERE id=?"); !e)
            return std::unexpected{e.error()};
        st.bindInt(1, id);
        int rc = st.stepDone();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db_))};
        if (sqlite3_changes(db_) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        return {};
    }

    std::expected<DbStats, caudio::utils::Error> getStats() {
        std::shared_lock lock{m_};
        if (!db_)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
        DbStats s;
        auto one = [&](std::string_view sql, int64_t &out) {
            Statement st;
            if (auto e = st.prepare(db_, sql); !e)
                return;
            if (st.step())
                out = st.columnInt(0);
        };
        one("SELECT COUNT(*) FROM tracks WHERE deleted_at IS NULL", s.num_tracks);
        one("SELECT COUNT(*) FROM playlists", s.num_playlists);
        one("SELECT COUNT(*) FROM queue", s.num_queue_items);
        one("SELECT COUNT(*) FROM history", s.num_history);
        one("SELECT COUNT(*) FROM bookmarks", s.num_bookmarks);
        one("SELECT COUNT(*) FROM libraries", s.num_libraries);
        Statement st;
        if (auto e = st.prepare(
                db_, "SELECT COALESCE(SUM(duration),0) FROM tracks WHERE deleted_at IS NULL");
            e) {
            if (st.step())
                s.total_duration_ms = (int64_t)(st.columnDouble(0) * 1000);
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
            // hex to bytes if 64 char
            if (fpHex.size() == 64) {
                auto hv = [](char c) -> int {
                    if (c >= '0' && c <= '9')
                        return c - '0';
                    if (c >= 'a' && c <= 'f')
                        return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F')
                        return c - 'A' + 10;
                    return -1;
                };
                for (int i = 0; i < 32; i++) {
                    int hi = hv(fpHex[i * 2]);
                    int lo = hv(fpHex[i * 2 + 1]);
                    if (hi < 0 || lo < 0)
                        break;
                    t.fingerprint[i] = (uint8_t)((hi << 4) | lo);
                }
            } else {
                // fallback random-ish
                std::memset(t.fingerprint.data(), 0, 32);
                for (size_t i = 0; i < fpHex.size() && i < 32; i++)
                    t.fingerprint[i] = (uint8_t)fpHex[i];
            }
        } else {
            // generate fingerprint from path via fnv fallback
            uint64_t h = 1469598103934665603ULL;
            for (char c : t.path) {
                h ^= (uint8_t)c;
                h *= 1099511628211ULL;
            }
            for (int i = 0; i < 32; i++) {
                t.fingerprint[i] = (uint8_t)(h >> ((i % 8) * 8));
                h = h * 6364136223846793005ULL + 1;
            }
        }
        auto r = insertTrack(t);
        if (!r)
            return std::unexpected{r.error()};
        return {};
    }

  private:
    sqlite3 *db_{nullptr};
    mutable std::shared_mutex m_;
};

} // namespace caudio::db
