#pragma once
#include <chrono>
#include <cstdint>
#include <expected>
#include <mutex>
#include <shared_mutex>
#include <sqlite3.h>
#include <string>
#include <vector>

#include "caudio/db/database.hpp"
#include "caudio/utils/utils.hpp"

/**
 * @file history.hpp
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

namespace caudio::engine {

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

namespace caudio::engine::detail {

/**
 * @brief Returns steady-clock time in milliseconds.
 * @ingroup caudio_engine
 * @return Milliseconds since steady_clock epoch.
 * @details Used for `startedMs_` and history timestamps; monotonic
 * so not affected by system clock changes.
 */
uint64_t nowMs() noexcept;

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
bool shouldMarkPlayedEx(double duration, double pos, bool marked, int pctThr,
                        int secsThr) noexcept;
/**
 * @brief Deprecated wrapper for shouldMarkPlayedEx with 60%/90s defaults.
 * @ingroup caudio_engine
 * @param duration Track duration in seconds.
 * @param pos Current position in seconds.
 * @param marked Whether already marked.
 * @return true if thresholds exceeded.
 * @deprecated Use shouldMarkPlayedEx; kept for tests (test_engine_history:40).
 */
bool shouldMarkPlayed(double duration, double pos, bool marked) noexcept;

} // namespace caudio::engine::detail

namespace caudio::engine {

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
    explicit History(std::shared_ptr<caudio::db::Database> db);

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
    ExpectedEntries listHistory(int limit = 50);

    /**
     * @brief Clears all history rows.
     * @ingroup caudio_engine
     * @return Success or Error.
     * @retval StatusCode::State if no db.
     * @retval StatusCode::Internal on sqlite3_exec failure.
     * @par Thread safety
     * Takes `unique_lock` on `db_->mutex()` and executes `DELETE FROM history`.
     */
    ExpectedVoid clearHistory();

  private:
    std::shared_ptr<caudio::db::Database> db_; ///< Shared DB handle.
};

} // namespace caudio::engine
