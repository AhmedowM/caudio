#pragma once

/**
 * @file database.hpp
 * @brief Main SQLite database handle with thread-safe CRUD.
 * @ingroup caudio_db
 * @details Implements `Database` with a shared_mutex for the SQLite handle,
 * a mutex for the prepared-statement cache, and a background writer thread
 * for batching writes. All public methods are thread-safe; see each method
 * for locking requirements. Move operations are not thread-safe after open.
 */

#include <array>
#include <caudio/db/types.hpp>
#include <caudio/fwd.hpp>
#include <caudio/utils/error.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string_view>
#include <vector>

namespace caudio::db {

/**
 * @brief Options for opening a Database.
 * @ingroup caudio_db
 */
struct DatabaseOptions {
    std::size_t writeBatchSize =
        256; ///< Queue capacity / batch size for the background writer (default 256).
    /// Non-owning logger (may be null = silent). Must outlive the Database;
    /// never emitted from destructors. Set from opts at open().
    caudio::utils::Logger* logger{nullptr};
};

/**
 * @brief Deprecated spelling of DatabaseOptions.
 * @ingroup caudio_db
 * @deprecated Use `DatabaseOptions` instead (kept until v2.0.0).
 */
using DbOpts [[deprecated("use DatabaseOptions instead")]] = DatabaseOptions; // legacy-shim-ok

/**
 * @brief Main SQLite database handle with thread-safe CRUD.
 * @ingroup caudio_db
 * @details Thread safety: dbMutex_ (shared_mutex) guards the handle and all
 * table access; cacheMutex_ (mutex) guards stmtCache_. Lock order is
 * dbMutex_ -> cacheMutex_. Readers take shared_lock on dbMutex_, writers
 * take unique_lock. getCachedForUse() requires the caller to already hold
 * cacheMutex_. Move operations are not thread-safe after open.
 * @see DatabaseOptions
 * @see SqliteStatement
 */
class Database final {
  public:
    /**
     * @brief Default-constructs an empty (not open) Database.
     * @ingroup caudio_db
     */
    Database();
    /**
     * @brief Constructs with options (sets the writer batch size).
     * @ingroup caudio_db
     * @param opts Options; writeBatchSize forwarded to the background writer.
     */
    // Out-of-line (core.cpp): Impl is defined there.
    explicit Database(const DatabaseOptions& opts);
    /**
     * @brief Closes the writer, clears the statement cache and resets the handle.
     * @ingroup caudio_db
     */
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    // Move is not thread-safe after open: caller must ensure no concurrent DB access.
    // The moved-from object is left empty; its accessors must not be used.
    Database(Database&& o) noexcept;
    Database& operator=(Database&& o) noexcept;
    /**
     * @brief Opens (or creates) a database.
     * @ingroup caudio_db
     * @param path Filesystem path (empty => :memory:).
     * @param opts Options (writeBatchSize).
     * @return Owning Database pointer or Error Io/Corrupt.
     * @details Executes schema, handles the queue UNIQUE migration gracefully
     * (ignores duplicate-position errors), ensures the default library row and
     * migrates engine_state.active_queue_id if missing. Starts the background
     * writer thread.
     * @par Thread safety
     * Thread-safe for distinct paths; the returned Database is not shared until open returns.
     */
    static std::expected<std::unique_ptr<Database>, caudio::utils::Error>
    open(std::string_view path, const DatabaseOptions& opts = {});
    /**
     * @brief Returns the non-owning logger from DatabaseOptions (may be null).
     * @ingroup caudio_db
     * @details For free functions (scan) that act on the database but take
     * no logger of their own. Never null-dereference; check before use.
     */
    caudio::utils::Logger* logger() const noexcept;
    /**
     * @brief Flushes the background writer queue.
     * @ingroup caudio_db
     * @return Success or Error with StatusCode::Busy on 200 ms timeout.
     * @par Thread safety
     * Thread-safe; delegates to the writer's flush().
     */
    std::expected<void, caudio::utils::Error> flush();

    /**
     * @brief Enqueues multiple tracks atomically (single transaction).
     * @ingroup caudio_db
     * @param qid Queue id (0 defaults to 1).
     * @param tids Track ids to enqueue.
     * @return Success or Error (Internal/Busy/NotFound).
     * @details Holds dbMutex_ exclusively and uses BEGIN IMMEDIATE / COMMIT
     * so concurrent listQueueItems never sees a partial batch.
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

  private:
    friend struct DatabaseAccess;
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    /**
     * @brief Track CRUD -- inserts, updates, deletes and queries tracks.
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
    std::expected<int64_t, caudio::utils::Error> insertTrack(const caudio::types::Track& t);
    /**
     * @brief Updates a track by id.
     * @ingroup caudio_db
     * @param t Track with fields to persist (id must be non-zero).
     * @return Success or Error InvalidArg/NotFound/Internal.
     * @par Thread safety
     * Thread-safe: unique_lock on dbMutex_.
     */
    std::expected<void, caudio::utils::Error> updateTrack(const caudio::types::Track& t);
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
    std::expected<caudio::types::Track, caudio::utils::Error> getTrack(int64_t id);

    /**
     * @brief Finds a track by fingerprint.
     * @ingroup caudio_db
     * @param fp Fingerprint to search.
     * @return Track or Error NotFound/Internal.
     * @par Thread safety
     * Thread-safe: shared_lock on dbMutex_.
     */
    std::expected<caudio::types::Track, caudio::utils::Error>
    findByFingerprint(const std::array<uint8_t, 32>& fp);
    /**
     * @brief Finds a track by path.
     * @ingroup caudio_db
     * @param path Path to search.
     * @return Track or Error NotFound/Internal.
     * @par Thread safety
     * Thread-safe: shared_lock on dbMutex_.
     */
    std::expected<caudio::types::Track, caudio::utils::Error> findByPath(std::string_view path);
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
    std::expected<std::vector<caudio::types::Track>, caudio::utils::Error>
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
    std::expected<caudio::types::Playlist, caudio::utils::Error> getPlaylist(int64_t id);
    /**
     * @brief Updates a playlist.
     * @ingroup caudio_db
     * @param p Playlist with updated fields (id must be non-zero).
     * @return Success or Error.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<void, caudio::utils::Error> updatePlaylist(const caudio::types::Playlist& p);
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
    /**
     * @brief Lists all playlists ordered by id.
     * @ingroup caudio_db
     * @return Vector of playlists or Error.
     * @par Thread safety
     * Thread-safe: shared_lock.
     */
    std::expected<std::vector<caudio::types::Playlist>, caudio::utils::Error> listPlaylists();
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
    std::expected<std::vector<caudio::types::Track>, caudio::utils::Error>
    playlistGetTracks(int64_t pid);

    /**
     * @brief Queue operations -- forwarded to queue partition (see queue.cppm).
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

    /**
     * @brief Dequeues the head item.
     * @ingroup caudio_db
     * @param qid Queue id.
     * @return QueueItem or Error NotFound.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<caudio::types::QueueItem, caudio::utils::Error> queueDequeue(int64_t qid);

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
    std::expected<std::vector<caudio::types::QueueItem>, caudio::utils::Error>
    listQueueItems(int64_t qid);

    /** @brief Deprecated spelling for listQueueItems(). @ingroup caudio_db */
    [[deprecated("use listQueueItems() instead")]]
    std::expected<std::vector<caudio::types::QueueItem>, caudio::utils::Error>
    queueList(int64_t qid) {
        return listQueueItems(qid);
    }

    std::expected<Queue, caudio::utils::Error> getQueue(int64_t qid);

    std::expected<std::vector<Queue>, caudio::utils::Error> listQueues();

    std::expected<int64_t, caudio::utils::Error> createQueue(std::string_view name,
                                                             int64_t library_id = 1);

    std::expected<void, caudio::utils::Error> deleteQueue(int64_t qid);

    std::expected<void, caudio::utils::Error> setQueueRepeat(int64_t qid, int repeat_mode);

    /**
     * @brief Sets the temporary flag for a queue.
     * @ingroup caudio_db
     * @param qid Queue id.
     * @param temp New flag.
     * @return Success or Error.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<void, caudio::utils::Error> setQueueTemp(int64_t qid, bool temp);

    /**
     * @brief Returns the number of items in a queue.
     * @ingroup caudio_db
     * @param qid Queue id (0 -> 1).
     * @return Item count.
     * @par Thread safety
     * Thread-safe: shared_lock on dbMutex_; delegates to queueCountLocked().
     */
    std::size_t queueCount(int64_t qid);

    /**
     * @brief Finds the 0-based index of a track in a queue.
     * @ingroup caudio_db
     * @param qid Queue id (0 -> 1).
     * @param trackId Track id to locate (first occurrence by position).
     * @return Index into the ORDER BY position listing, or std::nullopt if
     * the track is not in the queue.
     * @details O(log n) via the queue position index (see
     * queueIndexOfLocked); O(1) extra memory vs listQueueItems().
     * @par Thread safety
     * Thread-safe: shared_lock on dbMutex_.
     */
    std::optional<size_t> queueIndexOf(int64_t qid, int64_t trackId);

    // History
    /**
     * @brief Adds a history entry.
     * @ingroup caudio_db
     * @param e Entry to insert.
     * @return Success or Error.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<void, caudio::utils::Error> historyAdd(const caudio::types::HistoryEntry& e);
    /**
     * @brief Lists history entries with optional filters.
     * @ingroup caudio_db
     * @param q Optional filter.
     * @return Vector of entries or Error.
     * @par Thread safety
     * Thread-safe: shared_lock.
     */
    std::expected<std::vector<caudio::types::HistoryEntry>, caudio::utils::Error>
    listHistory(const HistoryQuery* q = nullptr);

    /** @brief Deprecated spelling for listHistory(). @ingroup caudio_db */
    [[deprecated("use listHistory() instead")]]
    std::expected<std::vector<caudio::types::HistoryEntry>, caudio::utils::Error>
    historyList(const HistoryQuery* q = nullptr) {
        return listHistory(q);
    }

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
    std::expected<std::vector<caudio::types::Bookmark>, caudio::utils::Error>
    listBookmarks(int64_t tid);

    /** @brief Deprecated spelling for listBookmarks(). @ingroup caudio_db */
    [[deprecated("use listBookmarks() instead")]]
    std::expected<std::vector<caudio::types::Bookmark>, caudio::utils::Error>
    bookmarkList(int64_t tid) {
        return listBookmarks(tid);
    }
    /**
     * @brief Gets a bookmark by id.
     * @ingroup caudio_db
     * @param id Bookmark id.
     * @return Bookmark, InvalidArg, NotFound or Error.
     * @par Thread safety
     * Thread-safe: shared_lock.
     */
    std::expected<caudio::types::Bookmark, caudio::utils::Error> bookmarkGet(int64_t id);
    /**
     * @brief Removes a bookmark by id.
     * @ingroup caudio_db
     * @param id Bookmark id.
     * @return Success, InvalidArg, NotFound or Internal.
     * @par Thread safety
     * Thread-safe: unique_lock.
     */
    std::expected<void, caudio::utils::Error> bookmarkRemove(int64_t id);

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
    std::expected<std::vector<Library>, caudio::utils::Error> listLibraries();

    /** @brief Deprecated spelling for listLibraries(). @ingroup caudio_db */
    [[deprecated("use listLibraries() instead")]]
    std::expected<std::vector<Library>, caudio::utils::Error> libraryList() {
        return listLibraries();
    }
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
     * @brief Returns library stats (counts and totals, optionally most-played).
     * @ingroup caudio_db
     * @param mostPlayed Omitted for the summary projection (totals only); a
     * value requests that many most-played entries (0 = totals only).
     * @return Stats or Error.
     * @par Thread safety
     * Thread-safe: shared_lock + cacheMutex_.
     */
    std::expected<caudio::types::LibraryStats, caudio::utils::Error>
    libraryStats(std::optional<int> mostPlayed = std::nullopt);

    /** @brief Deprecated spelling for libraryStats(topN). @ingroup caudio_db */
    [[deprecated("use libraryStats(std::optional<int>) instead")]]
    std::expected<caudio::types::LibraryStats, caudio::utils::Error>
    libraryStatsDetailed(int topN = 10) {
        return libraryStats(topN);
    }

}; // class Database

} // namespace caudio::db
