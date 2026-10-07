#pragma once

#include <caudio/db/types.hpp>
#include <caudio/utils.hpp>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

/**
 * @file history.hpp
 * @brief Engine-internal history helper (NOT installed).
 * @details `History` is constructed by Engine only; the entry type is the
 * shared `caudio::db::HistoryEntry` (12 fields incl. track snapshots).
 */

namespace caudio::db {
class Database;
}

namespace caudio::engine::detail {

/**
 * @brief Returns steady-clock time in milliseconds.
 * @details Used for `startedMs_` and history timestamps; monotonic
 * so not affected by system clock changes.
 */
uint64_t nowMs() noexcept;

/**
 * @brief Tests whether playback should count as "played" for history.
 * @param duration Track duration in seconds (0 if unknown).
 * @param pos Current position in seconds.
 * @param marked Whether already marked (true => always false).
 * @param pctThr Percentage threshold 0..100 (60 => 0.6); 0 defaults to 60.
 * @param secsThr Absolute seconds threshold; 0 defaults to 90 s.
 * @return true if `pos/duration >= pct` or `pos >= secs` and not already marked.
 */
bool shouldMarkPlayed(double duration, double pos, bool marked, int pctThr, int secsThr) noexcept;

} // namespace caudio::engine::detail

namespace caudio::engine {

/**
 * @brief Thin history query helper over a Database handle.
 * @details Constructed with a `shared_ptr<Database>`; listHistory and
 * clearHistory operate directly on `db_` with the same locking
 * discipline as Database. Used by Engine::listHistory / clearHistory
 * which delegate to a temporary History instance.
 */
class History final {
  public:
    using ExpectedVoid = std::expected<void, caudio::utils::Error>; ///< Void or Error.
    using ExpectedEntries = std::expected<std::vector<caudio::db::HistoryEntry>,
                                          caudio::utils::Error>; ///< Entries or Error.

    /**
     * @brief Constructs a History helper.
     * @param db Shared Database handle (must be open).
     */
    explicit History(std::shared_ptr<caudio::db::Database> db);

    /**
     * @brief Lists recent history entries joined with track metadata.
     * @param limit Max rows to return (0 = no limit, default 50).
     * @return Vector of entries ordered by started_at DESC or Error.
     * @par Thread safety
     * Takes `shared_lock` on `db_->mutex()`; concurrent readers allowed,
     * writers (clearHistory / doHistoryMark) take `unique_lock`.
     */
    ExpectedEntries listHistory(int limit = 50);

    /**
     * @brief Clears all history rows.
     * @return Success or Error.
     * @par Thread safety
     * Takes `unique_lock` on `db_->mutex()` and executes `DELETE FROM history`.
     */
    ExpectedVoid clearHistory();

  private:
    std::shared_ptr<caudio::db::Database> db_; ///< Shared DB handle.
};

} // namespace caudio::engine
