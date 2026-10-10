#include <sqlite3.h>

#include <caudio/db/core.hpp>
#include <caudio/db/types.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/result.hpp>
#include <chrono>
#include <cstdint>
#include <engine/history.hpp>
#include <expected>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

namespace caudio::engine::detail {

uint64_t nowMs() noexcept {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

uint64_t wallMs() noexcept {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

bool shouldMarkPlayed(double duration, double pos, bool marked, int pctThr, int secsThr) noexcept {
    if (marked)
        return false;
    double pct = pctThr > 0 ? (double)pctThr / 100.0 : 0.6;
    double secs = secsThr > 0 ? (double)secsThr : 90.0;
    if (duration > 0.0 && pos / duration >= pct)
        return true;
    if (pos >= secs)
        return true;
    return false;
}

} // namespace caudio::engine::detail

namespace caudio::engine {

History::History(std::shared_ptr<caudio::db::Database> db) : db_(std::move(db)) {}

History::ExpectedEntries History::listHistory(int limit) {
    if (!db_ || !db_->handle())
        return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));

    std::string sql = "SELECT h.id, h.track_id, h.started_at, h.completed_at, h.position_ms, "
                      "h.completion_pct, h.queue_id, t.title, t.artist, t.path, t.duration "
                      "FROM history h "
                      "JOIN tracks t ON t.id = h.track_id "
                      "ORDER BY h.started_at DESC";

    bool hasLimit = limit > 0;
    if (hasLimit)
        sql += " LIMIT ?";

    sqlite3* h = db_->handle();
    std::shared_lock lk(db_->mutex());

    sqlite3_stmt* raw = nullptr;
    int rc = sqlite3_prepare_v2(h, sql.c_str(), -1, &raw, nullptr);
    if (rc != SQLITE_OK)
        return std::unexpected(
            caudio::utils::makeError(caudio::utils::StatusCode::Internal, sqlite3_errmsg(h)));

    struct StmtGuard {
        sqlite3_stmt* s;
        ~StmtGuard() {
            if (s)
                sqlite3_finalize(s);
        }
    } guard(raw);

    if (hasLimit)
        sqlite3_bind_int(raw, 1, limit);

    std::vector<caudio::db::HistoryEntry> out;
    while (sqlite3_step(raw) == SQLITE_ROW) {
        caudio::db::HistoryEntry e;
        e.id = sqlite3_column_int64(raw, 0);
        e.track_id = sqlite3_column_int64(raw, 1);
        e.started_at = sqlite3_column_int64(raw, 2);
        if (sqlite3_column_type(raw, 3) != SQLITE_NULL)
            e.completed_at = sqlite3_column_int64(raw, 3);
        e.position_ms = sqlite3_column_int64(raw, 4);
        e.completion_pct = sqlite3_column_double(raw, 5);
        e.queue_id = sqlite3_column_int64(raw, 6);
        e.title = sqlite3_column_text(raw, 7)
                      ? reinterpret_cast<const char*>(sqlite3_column_text(raw, 7))
                      : "";
        e.artist = sqlite3_column_text(raw, 8)
                       ? reinterpret_cast<const char*>(sqlite3_column_text(raw, 8))
                       : "";
        e.path = sqlite3_column_text(raw, 9)
                     ? reinterpret_cast<const char*>(sqlite3_column_text(raw, 9))
                     : "";
        e.duration = sqlite3_column_double(raw, 10);
        out.push_back(std::move(e));
    }
    return out;
}

History::ExpectedVoid History::clearHistory() {
    if (!db_ || !db_->handle())
        return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));

    std::unique_lock lk(db_->mutex());
    sqlite3* h = db_->handle();
    char* err = nullptr;
    int rc = sqlite3_exec(h, "DELETE FROM history", nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? std::string(err) : "clear history failed";
        sqlite3_free(err);
        return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::Internal, msg));
    }
    if (err)
        sqlite3_free(err);
    return {};
}

} // namespace caudio::engine
