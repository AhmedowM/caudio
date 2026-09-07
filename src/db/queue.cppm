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
    if (tid == 0)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    std::unique_lock<std::mutex> cacheLk(cacheMutex);
    auto getCached = [&](std::string_view sql) -> Statement* {
        std::string key(sql);
        auto it = stmtCache.find(key);
        if (it != stmtCache.end()) {
            it->second->reset();
            return it->second.get();
        }
        auto up = std::make_unique<Statement>();
        if (auto e = up->prepare(db, sql); !e)
            return nullptr;
        Statement* raw = up.get();
        stmtCache.emplace(std::move(key), std::move(up));
        return raw;
    };
    if (pos < 0) {
        if (Statement* ms =
                getCached("SELECT COALESCE(MAX(position), -1)+1 FROM queue WHERE queue_id=?")) {
            ms->bindInt(1, qid);
            if (ms->step())
                pos = ms->columnInt(0);
            ms->reset();
        }
        if (pos < 0)
            pos = 0;
    } else {
        if (Statement* ss = getCached(
                "UPDATE queue SET position=position+1 WHERE queue_id=? AND position>=?")) {
            ss->bindInt(1, qid);
            ss->bindInt(2, pos);
            (void)ss->stepDone();
            ss->reset();
        }
    }
    if (Statement* st =
            getCached("INSERT INTO queue (queue_id, track_id, position) VALUES (?,?,?)")) {
        st->bindInt(1, qid);
        st->bindInt(2, tid);
        st->bindInt(3, pos);
        int rc = st->stepDone();
        st->reset();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db))};
        return {};
    }
    return std::unexpected{
        caudio::utils::makeError(caudio::utils::Result::Internal, "stmt prepare failed")};
}

export std::expected<QueueItem, caudio::utils::Error>
queueDequeueLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    std::unique_lock<std::mutex> cacheLk(cacheMutex);
    auto getCached = [&](std::string_view sql) -> Statement* {
        std::string key(sql);
        auto it = stmtCache.find(key);
        if (it != stmtCache.end()) {
            it->second->reset();
            return it->second.get();
        }
        auto up = std::make_unique<Statement>();
        if (auto e = up->prepare(db, sql); !e)
            return nullptr;
        Statement* raw = up.get();
        stmtCache.emplace(std::move(key), std::move(up));
        return raw;
    };
    if (Statement* st = getCached("SELECT id, queue_id, track_id, position, added FROM queue "
                                  "WHERE queue_id=? ORDER BY position LIMIT 1")) {
        st->bindInt(1, qid);
        bool hasRow = st->step();
        if (!hasRow) {
            st->reset();
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        }
        QueueItem it;
        it.id = st->columnInt(0);
        it.queue_id = st->columnInt(1);
        it.trackId = st->columnInt(2);
        it.position = st->columnInt(3);
        it.added = st->columnInt(4);
        st->reset();
        if (Statement* del = getCached("DELETE FROM queue WHERE id=?")) {
            del->bindInt(1, it.id);
            (void)del->stepDone();
            del->reset();
        }
        if (Statement* sh =
                getCached("UPDATE queue SET position=position-1 WHERE queue_id=? AND position>?")) {
            sh->bindInt(1, qid);
            sh->bindInt(2, it.position);
            (void)sh->stepDone();
            sh->reset();
        }
        return it;
    }
    return std::unexpected{
        caudio::utils::makeError(caudio::utils::Result::Internal, "stmt prepare failed")};
}

export std::expected<void, caudio::utils::Error> queueRemoveLocked(sqlite3* db,
                                                                   std::mutex& cacheMutex,
                                                                   StmtCache& stmtCache,
                                                                   int64_t qid, int64_t pos) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    std::unique_lock<std::mutex> cacheLk(cacheMutex);
    auto getCached = [&](std::string_view sql) -> Statement* {
        std::string key(sql);
        auto it = stmtCache.find(key);
        if (it != stmtCache.end()) {
            it->second->reset();
            return it->second.get();
        }
        auto up = std::make_unique<Statement>();
        if (auto e = up->prepare(db, sql); !e)
            return nullptr;
        Statement* raw = up.get();
        stmtCache.emplace(std::move(key), std::move(up));
        return raw;
    };
    if (Statement* st = getCached("DELETE FROM queue WHERE queue_id=? AND position=?")) {
        st->bindInt(1, qid);
        st->bindInt(2, pos);
        int rc = st->stepDone();
        st->reset();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db))};
        if (sqlite3_changes(db) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        if (Statement* sh =
                getCached("UPDATE queue SET position=position-1 WHERE queue_id=? AND position>?")) {
            sh->bindInt(1, qid);
            sh->bindInt(2, pos);
            (void)sh->stepDone();
            sh->reset();
        }
        return {};
    }
    return std::unexpected{
        caudio::utils::makeError(caudio::utils::Result::Internal, "stmt prepare failed")};
}

export std::expected<void, caudio::utils::Error>
queueClearLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    std::unique_lock<std::mutex> cacheLk(cacheMutex);
    auto getCached = [&](std::string_view sql) -> Statement* {
        std::string key(sql);
        auto it = stmtCache.find(key);
        if (it != stmtCache.end()) {
            it->second->reset();
            return it->second.get();
        }
        auto up = std::make_unique<Statement>();
        if (auto e = up->prepare(db, sql); !e)
            return nullptr;
        Statement* raw = up.get();
        stmtCache.emplace(std::move(key), std::move(up));
        return raw;
    };
    if (Statement* st = getCached("DELETE FROM queue WHERE queue_id=?")) {
        st->bindInt(1, qid);
        int rc = st->stepDone();
        st->reset();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db))};
        return {};
    }
    return std::unexpected{
        caudio::utils::makeError(caudio::utils::Result::Internal, "stmt prepare failed")};
}

export std::expected<std::vector<QueueItem>, caudio::utils::Error>
queueListLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    std::unique_lock<std::mutex> cacheLk(cacheMutex);
    auto getCached = [&](std::string_view sql) -> Statement* {
        std::string key(sql);
        auto it = stmtCache.find(key);
        if (it != stmtCache.end()) {
            it->second->reset();
            return it->second.get();
        }
        auto up = std::make_unique<Statement>();
        if (auto e = up->prepare(db, sql); !e)
            return nullptr;
        Statement* raw = up.get();
        stmtCache.emplace(std::move(key), std::move(up));
        return raw;
    };
    if (Statement* st = getCached("SELECT id, queue_id, track_id, position, added FROM queue "
                                  "WHERE queue_id=? ORDER BY position")) {
        st->bindInt(1, qid);
        std::vector<QueueItem> out;
        while (st->step()) {
            QueueItem it;
            it.id = st->columnInt(0);
            it.queue_id = st->columnInt(1);
            it.trackId = st->columnInt(2);
            it.position = st->columnInt(3);
            it.added = st->columnInt(4);
            out.push_back(it);
        }
        st->reset();
        return out;
    }
    return std::unexpected{
        caudio::utils::makeError(caudio::utils::Result::Internal, "stmt prepare failed")};
}

export size_t queueCountLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache,
                               int64_t qid) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return 0;
    std::unique_lock<std::mutex> cacheLk(cacheMutex);
    auto getCached = [&](std::string_view sql) -> Statement* {
        std::string key(sql);
        auto it = stmtCache.find(key);
        if (it != stmtCache.end()) {
            it->second->reset();
            return it->second.get();
        }
        auto up = std::make_unique<Statement>();
        if (auto e = up->prepare(db, sql); !e)
            return nullptr;
        Statement* raw = up.get();
        stmtCache.emplace(std::move(key), std::move(up));
        return raw;
    };
    if (Statement* st = getCached("SELECT COUNT(*) FROM queue WHERE queue_id=?")) {
        st->bindInt(1, qid);
        size_t cnt = 0;
        if (st->step())
            cnt = (size_t)st->columnInt(0);
        st->reset();
        return cnt;
    }
    return 0;
}

export std::expected<Queue, caudio::utils::Error>
getQueueLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    std::unique_lock<std::mutex> cacheLk(cacheMutex);
    auto getCached = [&](std::string_view sql) -> Statement* {
        std::string key(sql);
        auto it = stmtCache.find(key);
        if (it != stmtCache.end()) {
            it->second->reset();
            return it->second.get();
        }
        auto up = std::make_unique<Statement>();
        if (auto e = up->prepare(db, sql); !e)
            return nullptr;
        Statement* raw = up.get();
        stmtCache.emplace(std::move(key), std::move(up));
        return raw;
    };
    if (Statement* st =
            getCached("SELECT id, name, repeat_mode, library_id FROM queues WHERE id=?")) {
        st->bindInt(1, qid);
        if (!st->step()) {
            st->reset();
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        }
        Queue q;
        q.id = st->columnInt(0);
        q.name = st->columnText(1);
        q.repeat_mode = st->columnInt(2);
        q.library_id = st->columnInt(3);
        st->reset();
        return q;
    }
    return std::unexpected{
        caudio::utils::makeError(caudio::utils::Result::Internal, "stmt prepare failed")};
}

export std::expected<std::vector<Queue>, caudio::utils::Error>
listQueuesLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache) {
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    std::unique_lock<std::mutex> cacheLk(cacheMutex);
    auto getCached = [&](std::string_view sql) -> Statement* {
        std::string key(sql);
        auto it = stmtCache.find(key);
        if (it != stmtCache.end()) {
            it->second->reset();
            return it->second.get();
        }
        auto up = std::make_unique<Statement>();
        if (auto e = up->prepare(db, sql); !e)
            return nullptr;
        Statement* raw = up.get();
        stmtCache.emplace(std::move(key), std::move(up));
        return raw;
    };
    if (Statement* st =
            getCached("SELECT id, name, repeat_mode, library_id FROM queues ORDER BY id")) {
        std::vector<Queue> out;
        while (st->step()) {
            Queue q;
            q.id = st->columnInt(0);
            q.name = st->columnText(1);
            q.repeat_mode = st->columnInt(2);
            q.library_id = st->columnInt(3);
            out.push_back(std::move(q));
        }
        st->reset();
        return out;
    }
    return std::unexpected{
        caudio::utils::makeError(caudio::utils::Result::Internal, "stmt prepare failed")};
}

export std::expected<int64_t, caudio::utils::Error>
createQueueLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, std::string_view name,
                  int64_t library_id = 1) {
    if (name.empty())
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg)};
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    std::unique_lock<std::mutex> cacheLk(cacheMutex);
    auto getCached = [&](std::string_view sql) -> Statement* {
        std::string key(sql);
        auto it = stmtCache.find(key);
        if (it != stmtCache.end()) {
            it->second->reset();
            return it->second.get();
        }
        auto up = std::make_unique<Statement>();
        if (auto e = up->prepare(db, sql); !e)
            return nullptr;
        Statement* raw = up.get();
        stmtCache.emplace(std::move(key), std::move(up));
        return raw;
    };
    if (Statement* st =
            getCached("INSERT INTO queues (name, repeat_mode, library_id) VALUES (?,?,?)")) {
        st->bindText(1, name);
        st->bindInt(2, 0); // RepeatMode::Off
        st->bindInt(3, library_id);
        int rc = st->stepDone();
        st->reset();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db))};
        return sqlite3_last_insert_rowid(db);
    }
    return std::unexpected{
        caudio::utils::makeError(caudio::utils::Result::Internal, "stmt prepare failed")};
}

export std::expected<void, caudio::utils::Error>
deleteQueueLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    std::unique_lock<std::mutex> cacheLk(cacheMutex);
    auto getCached = [&](std::string_view sql) -> Statement* {
        std::string key(sql);
        auto it = stmtCache.find(key);
        if (it != stmtCache.end()) {
            it->second->reset();
            return it->second.get();
        }
        auto up = std::make_unique<Statement>();
        if (auto e = up->prepare(db, sql); !e)
            return nullptr;
        Statement* raw = up.get();
        stmtCache.emplace(std::move(key), std::move(up));
        return raw;
    };
    // Delete queue items first
    if (Statement* st = getCached("DELETE FROM queue WHERE queue_id=?")) {
        st->bindInt(1, qid);
        (void)st->stepDone();
        st->reset();
    }
    // Delete queue itself
    if (Statement* st = getCached("DELETE FROM queues WHERE id=?")) {
        st->bindInt(1, qid);
        int rc = st->stepDone();
        st->reset();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db))};
        if (sqlite3_changes(db) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        return {};
    }
    return std::unexpected{
        caudio::utils::makeError(caudio::utils::Result::Internal, "stmt prepare failed")};
}

export std::expected<void, caudio::utils::Error>
setQueueRepeatLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid,
                     int repeat_mode) {
    if (qid == 0)
        qid = 1;
    if (!db)
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Internal, "no db")};
    std::unique_lock<std::mutex> cacheLk(cacheMutex);
    auto getCached = [&](std::string_view sql) -> Statement* {
        std::string key(sql);
        auto it = stmtCache.find(key);
        if (it != stmtCache.end()) {
            it->second->reset();
            return it->second.get();
        }
        auto up = std::make_unique<Statement>();
        if (auto e = up->prepare(db, sql); !e)
            return nullptr;
        Statement* raw = up.get();
        stmtCache.emplace(std::move(key), std::move(up));
        return raw;
    };
    if (Statement* st = getCached("UPDATE queues SET repeat_mode=? WHERE id=?")) {
        st->bindInt(1, repeat_mode);
        st->bindInt(2, qid);
        int rc = st->stepDone();
        st->reset();
        if (rc != SQLITE_DONE)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::Result::Internal, sqlite3_errmsg(db))};
        if (sqlite3_changes(db) == 0)
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound)};
        return {};
    }
    return std::unexpected{
        caudio::utils::makeError(caudio::utils::Result::Internal, "stmt prepare failed")};
}

// Alias for queueListLocked
export std::expected<std::vector<QueueItem>, caudio::utils::Error>
getQueueItemsLocked(sqlite3* db, std::mutex& cacheMutex, StmtCache& stmtCache, int64_t qid) {
    return queueListLocked(db, cacheMutex, stmtCache, qid);
}

} // namespace caudio::db