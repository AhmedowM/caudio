#include <caudio/utils/error.hpp>
#include <caudio/utils/result.hpp>
#include <chrono>
#include <cstdint>
#include <engine/history.hpp>
#include <expected>
#include <memory>
#include <utility>

namespace caudio::engine::detail {

uint64_t nowMs() noexcept {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

uint64_t wallMs() noexcept {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

bool shouldMarkPlayed(double duration, double played, bool marked, int pctThr, int secsThr) noexcept {
    if (marked)
        return false;
    double pct = pctThr > 0 ? (double)pctThr / 100.0 : 0.6;
    double secs = secsThr > 0 ? (double)secsThr : 90.0;
    if (duration > 0.0 && played / duration >= pct)
        return true;
    if (played >= secs)
        return true;
    return false;
}

} // namespace caudio::engine::detail

namespace caudio::engine {

History::History(std::shared_ptr<caudio::contracts::IDatabase> db) : db_(std::move(db)) {}

History::ExpectedEntries History::listHistory(int limit) {
    if (!db_ || !db_->isOpen())
        return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
    return db_->engineHistoryList(limit);
}

History::ExpectedVoid History::clearHistory() {
    if (!db_ || !db_->isOpen())
        return std::unexpected(caudio::utils::makeError(caudio::utils::StatusCode::State, "no db"));
    return db_->engineHistoryClear();
}

} // namespace caudio::engine
