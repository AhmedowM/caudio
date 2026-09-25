#pragma once

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

#include <caudio/db/db_types.hpp>
#include <caudio/db/detail.hpp>
#include <caudio/db/queue.hpp>
#include <caudio/db/schema.hpp>
#include <caudio/db/statement.hpp>
#include <caudio/db/transaction.hpp>
#include <caudio/db/write_thread.hpp>
#include <caudio/utils.hpp>

// Forward declarations for SQLite handles (sqlite3.h stays in .cpp files).
struct sqlite3;
struct sqlite3_stmt;

namespace caudio::db {

/**
 * @brief Options for opening a Database.
 * @ingroup caudio_db
 */
struct DbOpts {
    std::size_t writeBatchSize =
        256; ///< Queue capacity / batch size for WriterThread (default 256).
};

/**
 * @brief Closes an owned SQLite handle (defined in db_core.cpp).
 * @ingroup caudio_db
 */
struct SqliteCloser {
    void operator()(sqlite3* db) const noexcept;
};

/**
 * @brief Main SQLite database handle with thread-safe CRUD.
 * @ingroup caudio_db
 * @details Thread safety: dbMutex_ (shared_mutex) guards the handle and all
 * table access; cacheMutex_ (mutex) guards stmtCache_. Lock order is
 * dbMutex_ -> cacheMutex_. Readers take shared_lock on dbMutex_, writers
 * take unique_lock. getCachedForUse() requires the caller to already hold
 * cacheMutex_. Move operations are not thread-safe after open.
 * @see DbOpts
 * @see WriterThread
 * @see SqliteStatement
 */
class Database final {
  public:
    /**
     * @brief Default-constructs an empty (not open) Database.
     * @ingroup caudio_db
     */
    Database() = default;
    /**
     * @brief Constructs with options (sets WriterThread batch size).
     * @ingroup caudio_db
     * @param opts Options; writeBatchSize forwarded to WriterThread.
     */
    explicit Database(const DbOpts& opts) : writer_(opts.writeBatchSize), db_(nullptr) {}
    /**
     * @brief Closes the writer, clears the statement cache and resets the handle.
     * @ingroup caudio_db
     */
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    // Move is not thread-safe after open: caller must ensure no concurrent DB access.
    // If move is used, lock order must be dbMutex_ (dbMutex_) -> stmtCacheMutex_ (cacheMutex_)
    // to match normal operation (unique_lock dbMutex_ then cacheMutex_).
    Database(Database&& o) noexcept;
    Database& operator=(Database&& o) noexcept;
    /**
     * @brief Clears the prepared-statement cache.
     * @ingroup caudio_db
     * @par Thread safety
     * Thread-safe: locks cacheMutex_.
     */
    void clearCache() const;
    // per-connection prepared SqliteStatement cache
    // Primary API: getCachedForUse returns expected; callers must check.
    // Legacy raw-pointer APIs are deprecated and delegate to expected (nullptr only on error).
    /**
     * @brief Gets or prepares a cached statement (caller must hold cacheMutex_).
     * @ingroup caudio_db
     * @param sql SQL text used as cache key.
     * @return Prepared statement pointer, or Error (Internal) if prepare fails.
     * @details Cache is per-connection; caller must hold cacheMutex_ (e.g. via
     * unique_lock<mutex> on cacheMutex_) before calling. Resets the statement
     * before returning. Thread safety: requires external lock on cacheMutex_.
     * @par Lock ordering
     * If both locks are needed, acquire dbMutex_ before cacheMutex_.
     */
    /**
     * @brief Inserts a track (caller holds dbMutex_).
     * @ingroup caudio_db
     * @par Thread safety
     * Caller must hold dbMutex_; locks cacheMutex_ where needed.
     */
    /**
     * @brief Updates a track by id (caller holds dbMutex_).
     * @ingroup caudio_db
     * @par Thread safety
     * Caller must hold dbMutex_; locks cacheMutex_ where needed.
     */
    /**
     * @brief Deletes a track by id (caller holds dbMutex_).
     * @ingroup caudio_db
     * @par Thread safety
     * Caller must hold dbMutex_; locks cacheMutex_ where needed.
     */
    /**
     * @brief Lists all libraries (caller holds dbMutex_ if outer lock held, otherwise shared).
     * @ingroup caudio_db
     * @par Thread safety
     * Caller must hold dbMutex_; locks cacheMutex_ where needed.
     */
    /**
     * @brief Updates a library row (caller holds dbMutex_).
     * @ingroup caudio_db
     * @par Thread safety
     * Caller must hold dbMutex_; locks cacheMutex_ where needed.
     */
    std::expected<SqliteStatement*, caudio::utils::Error>
    getCachedForUse(std::string_view sql) const;
    /**
     * @brief Opens (or creates) a database.
     * @ingroup caudio_db
     * @param path Filesystem path (empty => :memory:).
     * @param opts Options (writeBatchSize).
     * @return Owning Database pointer or Error Io/Corrupt.
     * @details Executes kSchema, handles the queue UNIQUE migration gracefully
     * (ignores duplicate-position errors), ensures the default library row and
     * migrates engine_state.active_queue_id if missing. Starts the WriterThread.
     * @par Thread safety
     * Thread-safe for distinct paths; the returned Database is not shared until open returns.
     */
    static std::expected<std::unique_ptr<Database>, caudio::utils::Error>
    open(std::string_view path, const DbOpts& opts = {});
    /**
     * @brief Returns the raw sqlite3 handle (no lock).
     * @ingroup caudio_db
     * @return Borrowed handle, may be null if not open.
     * @warning Not thread-safe; prefer handleLocked() when holding dbMutex_.
     */
    sqlite3* handle() const;
    /**
     * @brief Returns the database mutex (dbMutex_).
     * @ingroup caudio_db
     * @return Reference to the shared_mutex guarding the handle and tables.
     * @details Shared for reads, exclusive for writes/transactions.
     */
    std::shared_mutex& mutex() const;
    /**
     * @brief Returns the handle assuming the caller holds dbMutex_.
     * @ingroup caudio_db
     * @return Borrowed handle.
     * @par Thread safety
     * Caller must hold dbMutex_ (shared or exclusive).
     */
    sqlite3* handleLocked() const noexcept;
    /**
     * @brief Flushes the background WriterThread queue.
     * @ingroup caudio_db
     * @return Success or Error with StatusCode::Busy on 200 ms timeout.
     * @par Thread safety
     * Thread-safe; delegates to WriterThread::flush().
     * @see WriterThread::flush
     */
    std::expected<void, caudio::utils::Error> flush();

    /**
     * @brief Enqueues multiple tracks atomically (single transaction).
     * @ingroup caudio_db
     * @param qid Queue id (0 defaults to 1).
     * @param tids Track ids to enqueue.
     * @return Success or Error (Internal/Busy/NotFound).
     * @details Holds dbMutex_ exclusively and uses BEGIN IMMEDIATE / COMMIT
     * so concurrent queueList never sees a partial batch.
     * @par Thread safety
     * Thread-safe: unique_lock on dbMutex_.
     * @see queueReplaceAll
     */
    std::expected<void, caudio::utils::Error> queueEnqueueBatch(int64_t qid,
                                                                std::span<const int64_t> tids);
    std::expected<void, caudio::utils::Error> queueEnqueueBatch(int64_t qid,
                                                                const std::vector<int64_t>& tids);

    /**
     * @brief Replaces an entire queue atomically.
     * @ingroup caudio_db
     * @param qid Queue id (0 defaults to 1).
     * @param ids Track ids to set (clears then enqueues).
     * @return Success or Error.
     * @details Single transaction: queueClearLocked + queueEnqueueLocked per id.
     * @par Thread safety
     * Thread-safe: unique_lock on dbMutex_.
     */
    std::expected<void, caudio::utils::Error> queueReplaceAll(int64_t qid,
                                                              std::span<const int64_t> ids);
    std::expected<void, caudio::utils::Error> queueReplaceAll(int64_t qid,
                                                              const std::vector<int64_t>& ids);

    /**
     * @brief Locked helper section â€” caller must hold Database::mutex().
     * @ingroup caudio_db
     * @details These avoid re-locking dbMutex_ and are intended for use inside
     * outer transactions/batches (e.g., scanLibrary).
     */
    /**
     * @brief Finds a track by path (caller holds dbMutex_).
     * @ingroup caudio_db
     * @param path Exact path to search.
     * @return Track or Error NotFound/Internal.
     * @par Thread safety
     * Caller must hold dbMutex_ (shared or exclusive); internally locks cacheMutex_.
     * @see findByPath
     */
    std::expected<Track, caudio::utils::Error> findByPathLocked(std::string_view path);
    /**
     * @brief Finds a track by fingerprint (caller holds dbMutex_).
     * @ingroup caudio_db
     * @param fp 32-byte fingerprint.
     * @return Track or Error NotFound/Internal.
     * @par Thread safety
     * Caller must hold dbMutex_; locks cacheMutex_ internally.
     */
    std::expected<Track, caudio::utils::Error>
    findByFingerprintLocked(const std::array<uint8_t, 32>& fp);
    /**
     * @brief Inserts a track (caller holds dbMutex_).
     * @ingroup caudio_db
     * @param t Track to insert (fingerprint must be unique).
     * @return New row id or Error AlreadyExists/Internal.
     * @par Thread safety
     * Caller must hold dbMutex_ exclusively; locks cacheMutex_ internally.
     * @see insertTrack
     * @see bindTrackForInsert
     */
    std::expected<int64_t, caudio::utils::Error> insertTrackLocked(const Track& t);
    /**
     * @brief Updates a track by id (caller holds dbMutex_).
     * @ingroup caudio_db
     * @param t Track with fields to persist (id must be non-zero).
     * @return Success or Error InvalidArg/NotFound/Internal.
     * @par Thread safety
     * Caller must hold dbMutex_ exclusively; locks cacheMutex_ internally.
     * @see updateTrack
     * @see bindTrackForUpdate
     */
    std::expected<void, caudio::utils::Error> updateTrackLocked(const Track& t);
    /**
     * @brief Deletes a track by id (caller holds dbMutex_).
     * @ingroup caudio_db
     * @param id Track id (0 is InvalidArg).
     * @return Success or Error NotFound/Internal.
     * @par Thread safety
     * Caller must hold dbMutex_ exclusively; locks cacheMutex_ internally.
     * @see deleteTrack
     */
    std::expected<void, caudio::utils::Error> deleteTrackLocked(int64_t id);
    /**
     * @brief Lists all libraries (caller holds dbMutex_).
     * @ingroup caudio_db
     * @return Vector of libraries or Error.
     * @par Thread safety
     * Caller must hold dbMutex_ (shared or exclusive).
     * @see libraryList
     */
    std::expected<std::vector<Library>, caudio::utils::Error> libraryListLocked();
    /**
     * @brief Updates a library row (caller holds dbMutex_).
     * @ingroup caudio_db
     * @param l Library with updated fields (id must be non-zero).
     * @return Success or Error InvalidArg/NotFound/Internal.
     * @par Thread safety
     * Caller must hold dbMutex_ exclusively.
     * @see libraryUpdate
     */
    std::expected<void, caudio::utils::Error> libraryUpdateLocked(const Library& l);

  private:
    WriterThread writer_;
    std::unique_ptr<sqlite3, SqliteCloser> db_;
    // Naming clarity: dbMutex_ is dbMutex_ (protects db_ handle and serializes DB ops),
    // cacheMutex_ is stmtCacheMutex_ (protects stmtCache_), queueLock_ concept maps to
    // engineQueueSpin_ in engine but DB uses dbMutex_ for queue table as well.
    // Lock order: dbMutex_ (dbMutex_) -> stmtCacheMutex_ (cacheMutex_)
    mutable std::shared_mutex dbMutex_;
    mutable std::unordered_map<std::string, std::unique_ptr<SqliteStatement>> stmtCache_;
    mutable std::mutex cacheMutex_; // stmtCacheMutex_

  private:
    // Inline helpers â€” reduce duplication between insert/update (internal::bindTrack coverage)
    /**
     * @brief Binds all Track fields for INSERT statement (24 parameters).
     * @ingroup caudio_db
     * @param st Prepared statement (kInsertTrackSql).
     * @param t Track to bind.
     * @details Parameter order matches kInsertTrackSql: fingerprint, path, size, mtime,
     * duration, sample_rate, channels, bitrate, title, artist, album, album_artist,
     * genre, year, track_num, disc_num, cover_art_path, rating, play_count,
     * last_played, date_added, last_scanned, dirty, library_id.
     * NULL binds for optional date_added.
     */
    static void bindTrackForInsert(SqliteStatement& st, const Track& t);
    /**
     * @brief Binds all Track fields for UPDATE statement (26 parameters).
     * @ingroup caudio_db
     * @param st Prepared statement (kUpdateTrackSql).
     * @param t Track to bind (id must be non-zero).
     * @details Parameter order matches kUpdateTrackSql: fingerprint, path, size, mtime,
     * duration, sample_rate, channels, bitrate, title, artist, album, album_artist,
     * genre, year, track_num, disc_num, cover_art_path, rating, play_count,
     * last_played, date_added, last_scanned, dirty, library_id, deleted_at, id.
     * NULL binds for optional deleted_at.
     */
    static void bindTrackForUpdate(SqliteStatement& st, const Track& t);
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
    /**
     * @brief Track CRUD â€” inserts, updates, deletes and queries tracks.
     * @ingroup caudio_db
     */
    /**
     * @brief Inserts a track.
     * @ingroup caudio_db
     * @param t Track to insert (fingerprint must be unique).
     * @return New row id or Error AlreadyExists/Internal.
     * @par Thread safety
     * Thread-safe: unique_lock on dbMutex_.
     * @see insertTrackLocked
     */
    std::expected<int64_t, caudio::utils::Error> insertTrack(const Track& t);
    /**
     * @brief Updates a track by id.
     * @ingroup caudio_db
     * @param t Track with fields to persist (id must be non-zero).
     * @return Success or Error InvalidArg/NotFound/Internal.
     * @par Thread safety
     * Thread-safe: unique_lock on dbMutex_.
     */
    std::expected<void, caudio::utils::Error> updateTrack(const Track& t);
    /**
     * @brief Deletes a track by id.
     * @ingroup caudio_db
     * @param id Track id (0 is InvalidArg).
     * @return Success or Error NotFound/Internal.
     * @par Thread safety
     * Thread-safe: unique_lock on dbMutex_.
     */
    std::expected<void, caudio::utils::Error> deleteTrack(int64_t id);
    /**
     * @brief Gets a track by id.
     * @ingroup caudio_db
     * @param id Track id.
     * @return Track or Error InvalidArg/NotFound/Internal.
     * @par Thread safety
     * Thread-safe: shared_lock on dbMutex_.
     */
    std::expected<Track, caudio::utils::Error> getTrack(int64_t id);

    /**
     * @brief Gets a track by id (caller holds dbMutex_).
     * @ingroup caudio_db
     * @param id Track id.
     * @return Track or Error.
     * @par Thread safety
     * Caller must hold dbMutex_; locks cacheMutex_.
     */
    std::expected<Track, caudio::utils::Error> getTrackLocked(int64_t id);
    /**
     * @brief Finds a track by fingerprint.
     * @ingroup caudio_db
     * @param fp Fingerprint to search.
     * @return Track or Error NotFound/Internal.
     * @par Thread safety
     * Thread-safe: shared_lock on dbMutex_.
     */
    std::expected<Track, caudio::utils::Error> findByFingerprint(const std::array<uint8_t, 32>& fp);
    /**
     * @brief Finds a track by path.
     * @ingroup caudio_db
     * @param path Path to search.
     * @return Track or Error NotFound/Internal.
     * @par Thread safety
     * Thread-safe: shared_lock on dbMutex_.
     */
    std::expected<Track, caudio::utils::Error> findByPath(std::string_view path);
    /**
     * @brief Lists tracks with optional filters.
     * @ingroup caudio_db
     * @param q Optional filter (nullptr = no filter, ordered by id).
     * @return Vector of tracks or Error Internal.
     * @details Builds WHERE clauses from TrackQuery fields; search uses LIKE ESCAPE.
     * @par Thread safety
     * Thread-safe: shared_lock on dbMutex_.
     * @see TrackQuery
     */
    std::expected<std::vector<Track>, caudio::utils::Error>
    listTracks(const TrackQuery* q = nullptr);
    /**
     * @brief Sets the dirty flag for a track.
     * @ingroup caudio_db
     * @param id Track id.
     * @param dirty Dirty value (0/1).
     * @return Success or Error NotFound/Internal.
     * @par Thread safety
     * Thread-safe: unique_lock on dbMutex_.
     */
    std::expected<void, caudio::utils::Error> setDirty(int64_t id, int dirty);

    // Playlists
    /**
     * @brief Creates a playlist.
     * @ingroup caudio_db
     * @param name Playlist name (must be non-empty).
     * @param type Playlist type (0 = manual).
     * @param smart_query Smart filter (empty for manual).
     * @return New id or Error InvalidArg/Internal.
     * @par Thread safety
     * Thread-safe: unique_lock on dbMutex_.
     */
    std::expected<int64_t, caudio::utils::Error> createPlaylist(std::string_view name, int type = 0,
                                                                std::string_view smart_query = {});
    /**
     * @brief Gets a playlist by id.
     * @ingroup caudio_db
     * @param id Playlist id.
     * @return Playlist or Error NotFound/InvalidArg.
     * @par Thread safety
     * Thread-safe: shared_lock.
     */
    std::expected<Playlist, caudio::utils::Error> getPlaylist(int64_t id);
    /**
     * @brief Updates a playlist.
     * @ingroup caudio_db
     * @param p Playlist with updated fields (id must be non-zero).
     * @return Success or Error.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<void, caudio::utils::Error> updatePlaylist(const Playlist& p);
    /**
     * @brief Deletes a playlist.
     * @ingroup caudio_db
     * @param id Playlist id.
     * @return Success or Error.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<void, caudio::utils::Error> deletePlaylist(int64_t id);
    /**
     * @brief Renames a playlist.
     * @ingroup caudio_db
     * @param id Playlist id.
     * @param newName New name (must be non-empty).
     * @return Success or Error.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<void, caudio::utils::Error> renamePlaylist(int64_t id, std::string_view newName);
    std::expected<int64_t, caudio::utils::Error>
    createPlaylistFromTracks(std::string_view name, std::span<const int64_t> trackIds);
    std::expected<int64_t, caudio::utils::Error>
    createPlaylistLocked(std::string_view name, int type = 0, std::string_view smart_query = {});
    std::expected<void, caudio::utils::Error> playlistAddTrackLocked(int64_t pid, int64_t tid,
                                                                     int64_t pos = -1);
    /**
     * @brief Lists all playlists ordered by id.
     * @ingroup caudio_db
     * @return Vector of playlists or Error.
     * @par Thread safety
     * Thread-safe: shared_lock.
     */
    std::expected<std::vector<Playlist>, caudio::utils::Error> listPlaylists();
    std::expected<void, caudio::utils::Error> playlistAddTrack(int64_t pid, int64_t tid,
                                                               int64_t pos = -1);
    std::expected<void, caudio::utils::Error> playlistRemoveTrack(int64_t pid, int64_t tid);
    std::expected<void, caudio::utils::Error> playlistReorder(int64_t pid, int64_t from,
                                                              int64_t to);
    /**
     * @brief Gets all tracks in a playlist ordered by position.
     * @ingroup caudio_db
     * @param pid Playlist id.
     * @return Tracks or Error.
     * @par Thread safety
     * Thread-safe: shared_lock.
     */
    std::expected<std::vector<Track>, caudio::utils::Error> playlistGetTracks(int64_t pid);

    /**
     * @brief Queue operations â€” forwarded to queue partition (see queue.cppm).
     * @ingroup caudio_db
     * @details All queue methods normalize qid == 0 to 1 and enforce the
     * UNIQUE(queue_id, position) invariant via dbMutex_ serialization.
     */
    /**
     * @brief Enqueues a track.
     * @ingroup caudio_db
     * @param qid Queue id (0 -> 1).
     * @param tid Track id.
     * @param pos Position (-1 = append).
     * @return Success or Error.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<void, caudio::utils::Error> queueEnqueue(int64_t qid, int64_t tid,
                                                           int64_t pos = -1);

    std::expected<void, caudio::utils::Error> queueEnqueueLocked(int64_t qid, int64_t tid,
                                                                 int64_t pos = -1);

    /**
     * @brief Dequeues the head item.
     * @ingroup caudio_db
     * @param qid Queue id.
     * @return QueueItem or Error NotFound.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<QueueItem, caudio::utils::Error> queueDequeue(int64_t qid);

    std::expected<QueueItem, caudio::utils::Error> queueDequeueLocked(int64_t qid);

    std::expected<QueueItem, caudio::utils::Error> queuePeekLocked(int64_t qid);

    /**
     * @brief Peeks the head item without removing.
     * @ingroup caudio_db
     * @param qid Queue id (0 -> 1).
     * @return QueueItem or Error NotFound.
     * @par Thread safety
     * Thread-safe: shared_lock.
     */
    std::expected<QueueItem, caudio::utils::Error> queuePeek(int64_t qid);

    /**
     * @brief Removes item at position.
     * @ingroup caudio_db
     * @param qid Queue id (0 -> 1).
     * @param pos Position to remove.
     * @return Success or Error NotFound.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<void, caudio::utils::Error> queueRemove(int64_t qid, int64_t pos);

    /**
     * @brief Clears a queue.
     * @ingroup caudio_db
     * @param qid Queue id (0 -> 1).
     * @return Success or Error.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<void, caudio::utils::Error> queueClear(int64_t qid);

    /**
     * @brief Lists all items in a queue ordered by position.
     * @ingroup caudio_db
     * @param qid Queue id (0 -> 1).
     * @return Vector of items or Error.
     * @par Thread safety
     * Thread-safe: shared_lock.
     */
    std::expected<std::vector<QueueItem>, caudio::utils::Error> queueList(int64_t qid);

    std::expected<std::vector<QueueItem>, caudio::utils::Error> getQueueItems(int64_t qid);

    std::expected<Queue, caudio::utils::Error> getQueue(int64_t qid);

    std::expected<std::vector<Queue>, caudio::utils::Error> listQueues();

    std::expected<int64_t, caudio::utils::Error> createQueue(std::string_view name,
                                                             int64_t library_id = 1);

    std::expected<void, caudio::utils::Error> deleteQueue(int64_t qid);

    std::expected<void, caudio::utils::Error> setQueueRepeat(int64_t qid, int repeat_mode);

    size_t queueCountLocked(int64_t qid);

    // History
    /**
     * @brief Adds a history entry.
     * @ingroup caudio_db
     * @param e Entry to insert.
     * @return Success or Error.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<void, caudio::utils::Error> historyAdd(const HistoryEntry& e);
    /**
     * @brief Lists history entries with optional filters.
     * @ingroup caudio_db
     * @param q Optional filter.
     * @return Vector of entries or Error.
     * @par Thread safety
     * Thread-safe: shared_lock.
     */
    std::expected<std::vector<HistoryEntry>, caudio::utils::Error>
    historyList(const HistoryQuery* q = nullptr);

    /**
     * @brief Clears all history.
     * @ingroup caudio_db
     * @return Success or Error.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<void, caudio::utils::Error> historyClear();

    // Bookmarks
    /**
     * @brief Adds a bookmark.
     * @ingroup caudio_db
     * @param tid Track id.
     * @param pos Position in ms.
     * @param note Optional note.
     * @return Success or Error InvalidArg/Internal.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<void, caudio::utils::Error> bookmarkAdd(int64_t tid, int64_t pos,
                                                          std::string_view note = {});
    /**
     * @brief Lists bookmarks for a track.
     * @ingroup caudio_db
     * @param tid Track id.
     * @return Bookmarks or Error.
     * @par Thread safety
     * Thread-safe: shared_lock.
     */
    std::expected<std::vector<Bookmark>, caudio::utils::Error> bookmarkList(int64_t tid);

    // Libraries
    /**
     * @brief Adds a library.
     * @ingroup caudio_db
     * @param path Root path (must be non-empty, UNIQUE).
     * @param name Display name.
     * @return New id or Error.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<int64_t, caudio::utils::Error> libraryAdd(std::string_view path,
                                                            std::string_view name = {});
    /**
     * @brief Lists all libraries.
     * @ingroup caudio_db
     * @return Vector of libraries or Error.
     * @par Thread safety
     * Thread-safe: shared_lock (delegates to libraryListLocked).
     */
    std::expected<std::vector<Library>, caudio::utils::Error> libraryList();
    std::expected<void, caudio::utils::Error> libraryUpdate(const Library& l);
    std::expected<void, caudio::utils::Error> libraryDelete(int64_t id);

    /**
     * @brief Returns aggregate DB stats.
     * @ingroup caudio_db
     * @return DbStats or Error.
     * @par Thread safety
     * Thread-safe: shared_lock + cacheMutex_ for cached statements.
     */
    std::expected<DbStats, caudio::utils::Error> getStats();

    /**
     * @brief Returns detailed library stats (counts, durations, top 10 most-played).
     * @ingroup caudio_db
     * @return Detailed stats or Error.
     * @par Thread safety
     * Thread-safe: shared_lock + cacheMutex_.
     */
    std::expected<caudio::db::LibraryStatsDetailedData, caudio::utils::Error>
    libraryStatsDetailed();

    // insert overload for legacy database.cppm signature
    std::expected<void, caudio::utils::Error> insertTrackLegacy(int64_t libraryId,
                                                                std::string_view name,
                                                                std::string_view path,
                                                                std::string_view fpHex = {});

}; // class Database

} // namespace caudio::db
