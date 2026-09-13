module;
#include <chrono>
#include <cstdint>
#include <expected>
#include <mutex>
#include <shared_mutex>
#include <sqlite3.h>
#include <string>
#include <vector>

export module caudio.engine:history;

import caudio.db;
import caudio.utils;

export namespace caudio::engine {

struct HistoryEntry {
    int64_t id{};
    int64_t track_id{};
    int64_t started_at{};
    int64_t completed_at{};
    int64_t position_ms{};
    double completion_pct{};
    int64_t queue_id{1};
    std::string title{};
    std::string artist{};
    std::string path{};
    double duration{};
};

} // namespace caudio::engine

export namespace caudio::engine::detail {

inline uint64_t nowMs() noexcept {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

inline bool shouldMarkPlayedEx(double duration, double pos, bool marked, int pctThr,
                               int secsThr) noexcept {
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
// DEPRECATED: use shouldMarkPlayedEx — kept for tests (test_engine_history:40)
inline bool shouldMarkPlayed(double duration, double pos, bool marked) noexcept {
    return shouldMarkPlayedEx(duration, pos, marked, 60, 90);
}

} // namespace caudio::engine::detail

export namespace caudio::engine {

class History final {
  public:
    using ExpectedVoid = std::expected<void, caudio::utils::Error>;
    using ExpectedEntries = std::expected<std::vector<HistoryEntry>, caudio::utils::Error>;

    explicit History(std::shared_ptr<caudio::db::Database> db) : db_(std::move(db)) {}

    ExpectedEntries listHistory(int limit = 50) {
        if (!db_ || !db_->handle())
            return std::unexpected(caudio::utils::makeError(
                caudio::utils::StatusCode::State, "no db"));

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
            return std::unexpected(caudio::utils::makeError(
                caudio::utils::StatusCode::Internal, sqlite3_errmsg(h)));

        struct StmtGuard {
            sqlite3_stmt* s;
            ~StmtGuard() { if (s) sqlite3_finalize(s); }
        } guard(raw);

        if (hasLimit)
            sqlite3_bind_int(raw, 1, limit);

        std::vector<HistoryEntry> out;
        while (sqlite3_step(raw) == SQLITE_ROW) {
            HistoryEntry e;
            e.id = sqlite3_column_int64(raw, 0);
            e.track_id = sqlite3_column_int64(raw, 1);
            e.started_at = sqlite3_column_int64(raw, 2);
            if (sqlite3_column_type(raw, 3) != SQLITE_NULL)
                e.completed_at = sqlite3_column_int64(raw, 3);
            e.position_ms = sqlite3_column_int64(raw, 4);
            e.completion_pct = sqlite3_column_double(raw, 5);
            e.queue_id = sqlite3_column_int64(raw, 6);
            e.title = sqlite3_column_text(raw, 7) ? reinterpret_cast<const char*>(sqlite3_column_text(raw, 7)) : "";
            e.artist = sqlite3_column_text(raw, 8) ? reinterpret_cast<const char*>(sqlite3_column_text(raw, 8)) : "";
            e.path = sqlite3_column_text(raw, 9) ? reinterpret_cast<const char*>(sqlite3_column_text(raw, 9)) : "";
            e.duration = sqlite3_column_double(raw, 10);
            out.push_back(std::move(e));
        }
        return out;
    }

    ExpectedVoid clearHistory() {
        if (!db_ || !db_->handle())
            return std::unexpected(caudio::utils::makeError(
                caudio::utils::StatusCode::State, "no db"));

        std::unique_lock lk(db_->mutex());
        sqlite3* h = db_->handle();
        char* err = nullptr;
        int rc = sqlite3_exec(h, "DELETE FROM history", nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? std::string(err) : "clear history failed";
            sqlite3_free(err);
            return std::unexpected(caudio::utils::makeError(
                caudio::utils::StatusCode::Internal, msg));
        }
        if (err) sqlite3_free(err);
        return {};
    }

  private:
    std::shared_ptr<caudio::db::Database> db_;
};

} // namespace caudio::engine
