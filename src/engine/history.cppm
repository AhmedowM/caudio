module;
#include <chrono>
#include <cstdint>
#include <expected>
#include <mutex>
#include <shared_mutex>
#include <sqlite3.h>
#include <string>
#include <vector>

/**
 * @file history.cppm
 * @brief Playback history types, thresholds and History class.
 * @ingroup caudio_engine
 * @details Defines HistoryEntry and the history-marking thresholds
 * (`historyThresholdPct` / `historyThresholdSecs`) that decide when a
 * play counts as completed. Engine::doHistoryMark consults
 * detail::shouldMarkPlayedEx and performs a single-writer CAS plus a
 * `BEGIN IMMEDIATE` transaction that bumps `tracks.play_count` and
 * inserts into `history`.
 *
 * Thread safety: History::listHistory uses `shared_lock` on
 * Database::mutex(); clearHistory uses `unique_lock`. Threshold
 * helpers are pure and `noexcept`.
 */

export module caudio.engine:history;

import caudio.db;
import caudio.utils;

export namespace caudio::engine {

/**
 * @brief Single history row joined with track metadata for display.
 * @ingroup caudio_engine
 * @details Row from `history h JOIN tracks t ON t.id = h.track_id`
 * ordered by `started_at DESC` in History::listHistory. `duration`
 * comes from `tracks.duration`.
 */
struct HistoryEntry {
    int64_t id{};            ///< History row id (PK).
    int64_t track_id{};      ///< Played track id (FK -> tracks.id).
    int64_t started_at{};    ///< Play start timestamp (steady-clock ms mapped to unix ms).
    int64_t completed_at{};  ///< Completion timestamp (0 if not marked).
    int64_t position_ms{};   ///< Position reached at mark time (ms).
    double completion_pct{}; ///< Completion percentage at mark time [0,100].
    int64_t queue_id{1};     ///< Queue context at play time.
    std::string title{};     ///< Track title snapshot (from tracks.title).
    std::string artist{};    ///< Track artist snapshot.
    std::string path{};      ///< Track filesystem path snapshot.
    double duration{};       ///< Track duration in seconds (from tracks.duration).
};

} // namespace caudio::engine

export namespace caudio::engine::detail {

/**
 * @brief Returns steady-clock time in milliseconds.
 * @ingroup caudio_engine
 * @return Milliseconds since steady_clock epoch.
 * @details Used for `startedMs_` and history timestamps; monotonic
 * so not affected by system clock changes.
 */
inline uint64_t nowMs() noexcept {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

/**
 * @brief Tests whether playback should count as "played" for history.
 * @ingroup caudio_engine
 * @param duration Track duration in seconds (0 if unknown).
 * @param pos Current position in seconds.
 * @param marked Whether already marked (true => always false).
 * @param pctThr Percentage threshold 0..100 (60 => 0.6); 0 defaults to 60.
 * @param secsThr Absolute seconds threshold; 0 defaults to 90 s.
 * @return true if `pos/duration >= pct` or `pos >= secs` and not already marked.
 * @details Mirrors the C engine's history-mark logic. Both thresholds
 * are ORed: either suffices. Defaults come from EngineConfig
 * (`historyThresholdPct`/`historyThresholdSecs`).
 * @see Engine::doHistoryMark
 * @see EngineConfig
 */
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
/**
 * @brief Deprecated wrapper for shouldMarkPlayedEx with 60%/90s defaults.
 * @ingroup caudio_engine
 * @param duration Track duration in seconds.
 * @param pos Current position in seconds.
 * @param marked Whether already marked.
 * @return true if thresholds exceeded.
 * @deprecated Use shouldMarkPlayedEx; kept for tests (test_engine_history:40).
 */
inline bool shouldMarkPlayed(double duration, double pos, bool marked) noexcept {
    return shouldMarkPlayedEx(duration, pos, marked, 60, 90);
}

} // namespace caudio::engine::detail

export namespace caudio::engine {

/**
 * @brief Thin history query helper over a Database handle.
 * @ingroup caudio_engine
 * @details Constructed with a `shared_ptr<Database>`; listHistory and
 * clearHistory operate directly on `db_` with the same locking
 * discipline as Database. Used by Engine::listHistory / clearHistory
 * which delegate to a temporary History instance.
 * @see HistoryEntry
 * @see detail::shouldMarkPlayedEx
 */
class History final {
  public:
    using ExpectedVoid = std::expected<void, caudio::utils::Error>; ///< Void or Error.
    using ExpectedEntries = std::expected<std::vector<HistoryEntry>, caudio::utils::Error>; ///< Entries or Error.

    /**
     * @brief Constructs a History helper.
     * @ingroup caudio_engine
     * @param db Shared Database handle (must be open).
     */
    explicit History(std::shared_ptr<caudio::db::Database> db) : db_(std::move(db)) {}

    /**
     * @brief Lists recent history entries joined with track metadata.
     * @ingroup caudio_engine
     * @param limit Max rows to return (0 = no limit, default 50).
     * @return Vector of HistoryEntry ordered by started_at DESC or Error.
     * @retval StatusCode::State if no db.
     * @retval StatusCode::Internal on prepare failure.
     * @par Thread safety
     * Takes `shared_lock` on `db_->mutex()`; concurrent readers allowed,
     * writers (clearHistory / doHistoryMark) take `unique_lock`.
     */
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

    /**
     * @brief Clears all history rows.
     * @ingroup caudio_engine
     * @return Success or Error.
     * @retval StatusCode::State if no db.
     * @retval StatusCode::Internal on sqlite3_exec failure.
     * @par Thread safety
     * Takes `unique_lock` on `db_->mutex()` and executes `DELETE FROM history`.
     */
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
    std::shared_ptr<caudio::db::Database> db_; ///< Shared DB handle.
};

} // namespace caudio::engine
