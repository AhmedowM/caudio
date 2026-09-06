module;
#include <chrono>
#include <cstdint>

export module caudio.engine:history_policy;

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
inline bool shouldMarkPlayed(double duration, double pos, bool marked) noexcept {
    return shouldMarkPlayedEx(duration, pos, marked, 60, 90);
}

} // namespace caudio::engine::detail
