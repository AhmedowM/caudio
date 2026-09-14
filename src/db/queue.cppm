module;
#include <sqlite3.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file queue.cppm
 * @brief Queue table helpers (single-writer invariant).
 * @ingroup caudio_db
 * @details All helpers are `*Locked` — the caller must hold the
 * Database mutex (dbMutex_) exclusively for mutating ops and at least
 * shared for reads. The `queue` table enforces UNIQUE(queue_id, position)
 * (see schema.cppm); position shifts use
 * `UPDATE queue SET position=position+1 WHERE queue_id=? AND position>=?`
 * which requires serialized access — guaranteed by the single-writer lock.
 * Every function normalizes qid == 0 to 1 (default queue).
 */

export module caudio.db:queue;

import caudio.utils;
import :types;
import :detail;
import :SqliteStatement;

namespace caudio::db {

/**
 * @brief Enqueues a track (caller holds DB mutex).
 * @ingroup caudio_db
 * @param db SQLite handle.
 * @param qid Queue id (0 -> 1).
 * @param tid Track id (must be non-zero).
 * @param pos Position (-1 = append at MAX+1, else shift + insert).
 * @return Success or Error with StatusCode::InvalidArg if tid==0,
 * StatusCode::Internal if no handle or SQLite error.
 * @details If pos < 0, computes MAX(position)+1; otherwise shifts
 * positions >= pos via position+1 before inserting. The UNIQUE invariant
 * requires exclusive lock (caller must hold dbMutex_).
 * @par Thread safety
 * Caller must hold Database::mutex().
 */
export std::expected<void, caudio::utils::Error> queueEnqueueLocked(sqlite3* db, int64_t qid,
                                                                    int64_t tid, int64_t pos = -1) {
    if (tid == 0)
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
    if (pos < 0) {
        SqliteStatement ms;
        if (auto e =
                ms.prepare(db, "SELECT COALESCE(MAX(position), -1)+1 FROM queue WHERE queue_id=?");
            e) {
            ms.bindInt(1, qid);
            if (ms.step())
                pos = ms.columnInt(0);
        } else {
            return std::unexpected{e.error()};
        }
        if (pos < 0)
            pos = 0;
    } else {
        SqliteStatement ss;
        if (auto e = ss.prepare(
                db, "UPDATE queue SET position=position+1 WHERE queue_id=? AND position>=?");
            e) {
            ss.bindInt(1, qid);
            ss.bindInt(2, pos);
            (void)ss.stepDone();
        } else {
            return std::unexpected{e.error()};
        }
    }
    SqliteStatement st;
    auto e = st.prepare(db, "INSERT INTO queue (queue_id, track_id, position) VALUES (?,?,?)");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, qid);
    st.bindInt(2, tid);
    st.bindInt(3, pos);
    int rc = st.stepDone();
    if (rc != SQLITE_DONE)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, sqlite3_errmsg(db))};
    return {};
}

/**
 * @brief Dequeues the head item (caller holds DB mutex).
 * @ingroup caudio_db
 * @param db SQLite handle.
 * @param qid Queue id (0 -> 1).
 * @return QueueItem or Error NotFound/Internal.
 * @details Selects ORDER BY position LIMIT 1, deletes it, then shifts
 * remaining positions down by 1 (position-1 where position > removed).
 * @par Thread safety
 * Caller must hold Database::mutex() exclusively.
 */
export std::expected<QueueItem, caudio::utils::Error> queueDequeueLocked(sqlite3* db, int64_t qid) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
    SqliteStatement st;
    auto e = st.prepare(db, "SELECT id, queue_id, track_id, position, added FROM queue "
                            "WHERE queue_id=? ORDER BY position LIMIT 1");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, qid);
    bool hasRow = st.step();
    if (!hasRow) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
    }
    QueueItem it;
    it.id = st.columnInt(0);
    it.queue_id = st.columnInt(1);
    it.track_id = st.columnInt(2);
    it.position = st.columnInt(3);
    it.added = st.columnInt(4);
    {
        SqliteStatement del;
        if (auto de = del.prepare(db, "DELETE FROM queue WHERE id=?"); de) {
            del.bindInt(1, it.id);
            (void)del.stepDone();
        }
    }
    {
        SqliteStatement sh;
        if (auto se = sh.prepare(
                db, "UPDATE queue SET position=position-1 WHERE queue_id=? AND position>?");
            se) {
            sh.bindInt(1, qid);
            sh.bindInt(2, it.position);
            (void)sh.stepDone();
        }
    }
    return it;
}

/**
 * @brief Peeks the head item without removing (caller holds DB mutex).
 * @ingroup caudio_db
 * @param db SQLite handle.
 * @param qid Queue id (0 -> 1).
 * @return QueueItem or Error NotFound/Internal.
 * @par Thread safety
 * Caller must hold Database::mutex() (shared or exclusive).
 */
export std::expected<QueueItem, caudio::utils::Error> queuePeekLocked(sqlite3* db, int64_t qid) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
    SqliteStatement st;
    auto e = st.prepare(db, "SELECT id, queue_id, track_id, position, added FROM queue "
                            "WHERE queue_id=? ORDER BY position LIMIT 1");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, qid);
    bool hasRow = st.step();
    if (!hasRow) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
    }
    QueueItem it;
    it.id = st.columnInt(0);
    it.queue_id = st.columnInt(1);
    it.track_id = st.columnInt(2);
    it.position = st.columnInt(3);
    it.added = st.columnInt(4);
    return it;
}

/**
 * @brief Removes item at position (caller holds DB mutex).
 * @ingroup caudio_db
 * @param db SQLite handle.
 * @param qid Queue id (0 -> 1).
 * @param pos Position to remove.
 * @return Success or Error NotFound/Internal.
 * @details Deletes WHERE queue_id=? AND position=?, then shifts down
 * positions > pos.
 * @par Thread safety
 * Caller must hold Database::mutex() exclusively.
 */
export std::expected<void, caudio::utils::Error> queueRemoveLocked(sqlite3* db, int64_t qid,
                                                                   int64_t pos) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
    SqliteStatement st;
    auto e = st.prepare(db, "DELETE FROM queue WHERE queue_id=? AND position=?");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, qid);
    st.bindInt(2, pos);
    int rc = st.stepDone();
    if (rc != SQLITE_DONE)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, sqlite3_errmsg(db))};
    if (sqlite3_changes(db) == 0)
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
    {
        SqliteStatement sh;
        if (auto se = sh.prepare(
                db, "UPDATE queue SET position=position-1 WHERE queue_id=? AND position>?");
            se) {
            sh.bindInt(1, qid);
            sh.bindInt(2, pos);
            (void)sh.stepDone();
        }
    }
    return {};
}

/**
 * @brief Clears all items in a queue (caller holds DB mutex).
 * @ingroup caudio_db
 * @param db SQLite handle.
 * @param qid Queue id (0 -> 1).
 * @return Success or Error Internal.
 * @par Thread safety
 * Caller must hold Database::mutex() exclusively.
 */
export std::expected<void, caudio::utils::Error> queueClearLocked(sqlite3* db, int64_t qid) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
    SqliteStatement st;
    auto e = st.prepare(db, "DELETE FROM queue WHERE queue_id=?");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, qid);
    int rc = st.stepDone();
    if (rc != SQLITE_DONE)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, sqlite3_errmsg(db))};
    return {};
}

/**
 * @brief Lists all items in a queue ordered by position (caller holds DB mutex).
 * @ingroup caudio_db
 * @param db SQLite handle.
 * @param qid Queue id (0 -> 1).
 * @return Vector of QueueItems or Error Internal.
 * @par Thread safety
 * Caller must hold Database::mutex() (shared or exclusive).
 */
export std::expected<std::vector<QueueItem>, caudio::utils::Error> queueListLocked(sqlite3* db,
                                                                                   int64_t qid) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
    SqliteStatement st;
    auto e = st.prepare(db, "SELECT id, queue_id, track_id, position, added FROM queue WHERE "
                            "queue_id=? ORDER BY position");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, qid);
    std::vector<QueueItem> out;
    while (st.step()) {
        QueueItem it;
        it.id = st.columnInt(0);
        it.queue_id = st.columnInt(1);
        it.track_id = st.columnInt(2);
        it.position = st.columnInt(3);
        it.added = st.columnInt(4);
        out.push_back(it);
    }
    return out;
}

/**
 * @brief Counts items in a queue (caller holds DB mutex).
 * @ingroup caudio_db
 * @param db SQLite handle.
 * @param qid Queue id (0 -> 1).
 * @return Count (0 if no db or on prepare failure).
 * @par Thread safety
 * Caller must hold Database::mutex() (shared or exclusive).
 */
export size_t queueCountLocked(sqlite3* db, int64_t qid) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return 0;
    SqliteStatement st;
    auto e = st.prepare(db, "SELECT COUNT(*) FROM queue WHERE queue_id=?");
    if (!e)
        return 0;
    st.bindInt(1, qid);
    size_t cnt = 0;
    if (st.step())
        cnt = (size_t)st.columnInt(0);
    return cnt;
}

/**
 * @brief Gets a queue container row (caller holds DB mutex).
 * @ingroup caudio_db
 * @param db SQLite handle.
 * @param qid Queue id (0 -> 1).
 * @return Queue or Error NotFound/Internal.
 * @par Thread safety
 * Caller must hold Database::mutex().
 */
export std::expected<Queue, caudio::utils::Error> getQueueLocked(sqlite3* db, int64_t qid) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
    SqliteStatement st;
    auto e = st.prepare(db, "SELECT id, name, repeat_mode, library_id FROM queues WHERE id=?");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, qid);
    if (!st.step()) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
    }
    Queue q;
    q.id = st.columnInt(0);
    q.name = st.columnText(1);
    q.repeat_mode = st.columnInt(2);
    q.library_id = st.columnInt(3);
    return q;
}

/**
 * @brief Lists all queue containers (caller holds DB mutex).
 * @ingroup caudio_db
 * @param db SQLite handle.
 * @return Vector of Queues or Error.
 * @par Thread safety
 * Caller must hold Database::mutex().
 */
export std::expected<std::vector<Queue>, caudio::utils::Error> listQueuesLocked(sqlite3* db) {
    if (!db)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
    SqliteStatement st;
    auto e = st.prepare(db, "SELECT id, name, repeat_mode, library_id FROM queues ORDER BY id");
    if (!e)
        return std::unexpected{e.error()};
    std::vector<Queue> out;
    while (st.step()) {
        Queue q;
        q.id = st.columnInt(0);
        q.name = st.columnText(1);
        q.repeat_mode = st.columnInt(2);
        q.library_id = st.columnInt(3);
        out.push_back(std::move(q));
    }
    return out;
}

/**
 * @brief Creates a queue container (caller holds DB mutex).
 * @ingroup caudio_db
 * @param db SQLite handle.
 * @param name Queue name (must be non-empty).
 * @param library_id Owning library.
 * @return New row id or Error InvalidArg/Internal.
 * @par Thread safety
 * Caller must hold Database::mutex() exclusively.
 */
export std::expected<int64_t, caudio::utils::Error>
createQueueLocked(sqlite3* db, std::string_view name, int64_t library_id = 1) {
    if (name.empty())
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg)};
    if (!db)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
    SqliteStatement st;
    auto e = st.prepare(db, "INSERT INTO queues (name, repeat_mode, library_id) VALUES (?,?,?)");
    if (!e)
        return std::unexpected{e.error()};
    st.bindText(1, name);
    st.bindInt(2, 0); // RepeatMode::Off
    st.bindInt(3, library_id);
    int rc = st.stepDone();
    if (rc != SQLITE_DONE)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, sqlite3_errmsg(db))};
    return sqlite3_last_insert_rowid(db);
}

/**
 * @brief Deletes a queue container and its items (caller holds DB mutex).
 * @ingroup caudio_db
 * @param db SQLite handle.
 * @param qid Queue id (0 -> 1).
 * @return Success or Error NotFound/Internal.
 * @details First deletes from queue (items), then from queues (container).
 * @par Thread safety
 * Caller must hold Database::mutex() exclusively.
 */
export std::expected<void, caudio::utils::Error> deleteQueueLocked(sqlite3* db, int64_t qid) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
    {
        SqliteStatement st;
        if (auto e = st.prepare(db, "DELETE FROM queue WHERE queue_id=?"); e) {
            st.bindInt(1, qid);
            (void)st.stepDone();
        } else {
            return std::unexpected{e.error()};
        }
    }
    SqliteStatement st;
    auto e = st.prepare(db, "DELETE FROM queues WHERE id=?");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, qid);
    int rc = st.stepDone();
    if (rc != SQLITE_DONE)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, sqlite3_errmsg(db))};
    if (sqlite3_changes(db) == 0)
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
    return {};
}

/**
 * @brief Sets the repeat mode for a queue (caller holds DB mutex).
 * @ingroup caudio_db
 * @param db SQLite handle.
 * @param qid Queue id (0 -> 1).
 * @param repeat_mode New mode.
 * @return Success or Error NotFound/Internal.
 * @par Thread safety
 * Caller must hold Database::mutex() exclusively.
 */
export std::expected<void, caudio::utils::Error> setQueueRepeatLocked(sqlite3* db, int64_t qid,
                                                                      int repeat_mode) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, "no db")};
    SqliteStatement st;
    auto e = st.prepare(db, "UPDATE queues SET repeat_mode=? WHERE id=?");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, repeat_mode);
    st.bindInt(2, qid);
    int rc = st.stepDone();
    if (rc != SQLITE_DONE)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, sqlite3_errmsg(db))};
    if (sqlite3_changes(db) == 0)
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound)};
    return {};
}

/**
 * @brief Alias for queueListLocked (caller holds DB mutex).
 * @ingroup caudio_db
 * @param db SQLite handle.
 * @param qid Queue id.
 * @return Vector of items or Error.
 * @par Thread safety
 * Caller must hold Database::mutex().
 * @see queueListLocked
 */
export std::expected<std::vector<QueueItem>, caudio::utils::Error>
getQueueItemsLocked(sqlite3* db, int64_t qid) {
    return queueListLocked(db, qid);
}

} // namespace caudio::db
