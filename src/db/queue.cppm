module;
#include <sqlite3.h>

#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module caudio.db:queue;

import caudio.utils;
import :types;
import :detail;
import :statement;

namespace caudio::db {

// Queue methods extracted from Database class - take explicit params to avoid module cycles
using StmtCache = std::unordered_map<std::string, std::unique_ptr<Statement>>;

export std::expected<void, caudio::utils::Error>
queueEnqueueLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid,
                   int64_t tid, int64_t pos = -1) {
    (void)cacheMutex;
    (void)stmtCache;
    if (tid == 0)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    if (pos < 0) {
        Statement ms;
        if (auto e = ms.prepare(db, "SELECT COALESCE(MAX(position), -1)+1 FROM queue WHERE queue_id=?"); e) {
            ms.bindInt(1, qid);
            if (ms.step())
                pos = ms.columnInt(0);
        } else {
            return std::unexpected{e.error()};
        }
        if (pos < 0)
            pos = 0;
    } else {
        Statement ss;
        if (auto e = ss.prepare(db, "UPDATE queue SET position=position+1 WHERE queue_id=? AND position>=?"); e) {
            ss.bindInt(1, qid);
            ss.bindInt(2, pos);
            (void)ss.stepDone();
        } else {
            return std::unexpected{e.error()};
        }
    }
    Statement st;
    auto e = st.prepare(db, "INSERT INTO queue (queue_id, track_id, position) VALUES (?,?,?)");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, qid);
    st.bindInt(2, tid);
    st.bindInt(3, pos);
    int rc = st.stepDone();
    if (rc != SQLITE_DONE)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db))};
    return {};
}

export std::expected<QueueItem, caudio::utils::Error>
queueDequeueLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid) {
    (void)cacheMutex;
    (void)stmtCache;
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    Statement st;
    auto e = st.prepare(db, "SELECT id, queue_id, track_id, position, added FROM queue "
                             "WHERE queue_id=? ORDER BY position LIMIT 1");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, qid);
    bool hasRow = st.step();
    if (!hasRow) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
    }
    QueueItem it;
    it.id = st.columnInt(0);
    it.queue_id = st.columnInt(1);
    it.trackId = st.columnInt(2);
    it.position = st.columnInt(3);
    it.added = st.columnInt(4);
    {
        Statement del;
        if (auto de = del.prepare(db, "DELETE FROM queue WHERE id=?"); de) {
            del.bindInt(1, it.id);
            (void)del.stepDone();
        }
    }
    {
        Statement sh;
        if (auto se = sh.prepare(db, "UPDATE queue SET position=position-1 WHERE queue_id=? AND position>?"); se) {
            sh.bindInt(1, qid);
            sh.bindInt(2, it.position);
            (void)sh.stepDone();
        }
    }
    return it;
}

export std::expected<QueueItem, caudio::utils::Error>
queuePeekLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid) {
    (void)cacheMutex;
    (void)stmtCache;
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    Statement st;
    auto e = st.prepare(db, "SELECT id, queue_id, track_id, position, added FROM queue "
                             "WHERE queue_id=? ORDER BY position LIMIT 1");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, qid);
    bool hasRow = st.step();
    if (!hasRow) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
    }
    QueueItem it;
    it.id = st.columnInt(0);
    it.queue_id = st.columnInt(1);
    it.trackId = st.columnInt(2);
    it.position = st.columnInt(3);
    it.added = st.columnInt(4);
    return it;
}

export std::expected<void, caudio::utils::Error> queueRemoveLocked(sqlite3* db,
                                                                   std::mutex& cacheMutex,
                                                                   StmtCache& stmtCache,
                                                                   int64_t qid, int64_t pos) {
    (void)cacheMutex;
    (void)stmtCache;
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    Statement st;
    auto e = st.prepare(db, "DELETE FROM queue WHERE queue_id=? AND position=?");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, qid);
    st.bindInt(2, pos);
    int rc = st.stepDone();
    if (rc != SQLITE_DONE)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db))};
    if (sqlite3_changes(db) == 0)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
    {
        Statement sh;
        if (auto se = sh.prepare(db, "UPDATE queue SET position=position-1 WHERE queue_id=? AND position>?"); se) {
            sh.bindInt(1, qid);
            sh.bindInt(2, pos);
            (void)sh.stepDone();
        }
    }
    return {};
}

export std::expected<void, caudio::utils::Error>
queueClearLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid) {
    (void)cacheMutex;
    (void)stmtCache;
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    Statement st;
    auto e = st.prepare(db, "DELETE FROM queue WHERE queue_id=?");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, qid);
    int rc = st.stepDone();
    if (rc != SQLITE_DONE)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db))};
    return {};
}

export std::expected<std::vector<QueueItem>, caudio::utils::Error>
queueListLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid) {
    (void)cacheMutex;
    (void)stmtCache;
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    Statement st;
    auto e = st.prepare(db, "SELECT id, queue_id, track_id, position, added FROM queue WHERE queue_id=? ORDER BY position");
    if (!e)
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

export size_t queueCountLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache,
                               int64_t qid) {
    (void)cacheMutex;
    (void)stmtCache;
    if (qid == 0)
        qid = 1;
    if (!db)
        return 0;
    Statement st;
    auto e = st.prepare(db, "SELECT COUNT(*) FROM queue WHERE queue_id=?");
    if (!e)
        return 0;
    st.bindInt(1, qid);
    size_t cnt = 0;
    if (st.step())
        cnt = (size_t)st.columnInt(0);
    return cnt;
}

export std::expected<Queue, caudio::utils::Error>
getQueueLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid) {
    (void)cacheMutex;
    (void)stmtCache;
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    Statement st;
    auto e = st.prepare(db, "SELECT id, name, repeat_mode, library_id FROM queues WHERE id=?");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, qid);
    if (!st.step()) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
    }
    Queue q;
    q.id = st.columnInt(0);
    q.name = st.columnText(1);
    q.repeat_mode = st.columnInt(2);
    q.library_id = st.columnInt(3);
    return q;
}

export std::expected<std::vector<Queue>, caudio::utils::Error>
listQueuesLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache) {
    (void)cacheMutex;
    (void)stmtCache;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    Statement st;
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

export std::expected<int64_t, caudio::utils::Error>
createQueueLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, std::string_view name,
                  int64_t library_id = 1) {
    (void)cacheMutex;
    (void)stmtCache;
    if (name.empty())
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    Statement st;
    auto e = st.prepare(db, "INSERT INTO queues (name, repeat_mode, library_id) VALUES (?,?,?)");
    if (!e)
        return std::unexpected{e.error()};
    st.bindText(1, name);
    st.bindInt(2, 0); // RepeatMode::Off
    st.bindInt(3, library_id);
    int rc = st.stepDone();
    if (rc != SQLITE_DONE)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db))};
    return sqlite3_last_insert_rowid(db);
}

export std::expected<void, caudio::utils::Error>
deleteQueueLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid) {
    (void)cacheMutex;
    (void)stmtCache;
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    {
        Statement st;
        if (auto e = st.prepare(db, "DELETE FROM queue WHERE queue_id=?"); e) {
            st.bindInt(1, qid);
            (void)st.stepDone();
        } else {
            return std::unexpected{e.error()};
        }
    }
    Statement st;
    auto e = st.prepare(db, "DELETE FROM queues WHERE id=?");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, qid);
    int rc = st.stepDone();
    if (rc != SQLITE_DONE)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db))};
    if (sqlite3_changes(db) == 0)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
    return {};
}

export std::expected<void, caudio::utils::Error>
setQueueRepeatLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid,
                     int repeat_mode) {
    (void)cacheMutex;
    (void)stmtCache;
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    Statement st;
    auto e = st.prepare(db, "UPDATE queues SET repeat_mode=? WHERE id=?");
    if (!e)
        return std::unexpected{e.error()};
    st.bindInt(1, repeat_mode);
    st.bindInt(2, qid);
    int rc = st.stepDone();
    if (rc != SQLITE_DONE)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db))};
    if (sqlite3_changes(db) == 0)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
    return {};
}

export std::expected<std::vector<QueueItem>, caudio::utils::Error>
getQueueItemsLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid) {
    return queueListLocked(db, cacheMutex, stmtCache, qid);
}

} // namespace caudio::db
