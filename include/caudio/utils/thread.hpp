#pragma once

#include <caudio/utils/error.hpp>
#include <caudio/utils/result.hpp>
#include <chrono>
#include <cstdint>
#include <expected>
#include <string_view>
#include <thread>

namespace caudio::utils::detail {

constexpr std::string_view truncate15(std::string_view s) noexcept {
    return s.substr(0, 15);
}

} // namespace caudio::utils::detail

namespace caudio::utils {

/**
 * @brief Sleeps for a chrono duration.
 * @ingroup caudio_utils
 * @tparam Rep Duration rep type.
 * @tparam Period Duration period type.
 * @param d Duration to sleep.
 * @details Thin wrapper around `std::this_thread::sleep_for`; noexcept.
 */
template <typename Rep, typename Period>
inline void sleepFor(std::chrono::duration<Rep, Period> d) noexcept {
    std::this_thread::sleep_for(d);
}

/**
 * @brief Sleeps for a number of milliseconds.
 * @ingroup caudio_utils
 * @param ms Milliseconds to sleep.
 * @details Delegates to sleepFor(std::chrono::milliseconds).
 */
inline void sleepForMs(std::uint32_t ms) noexcept {
    sleepFor(std::chrono::milliseconds(ms));
}

/**
 * @brief Sets the current thread's name.
 * @ingroup caudio_utils
 * @param name Non-empty name view (UTF-8 on Windows).
 * @return Expected<void> success, or error with codes:
 * - `InvalidArg` if `name` empty,
 * - `Unsupported` if the platform does not support thread naming.
 * @details Implemented per-platform in `src/utils/thread.cpp`:
 * - Windows: `SetThreadDescription` via dynamic `GetProcAddress`;
 *   if unavailable, returns success (no-op for compat).
 * - macOS: `pthread_setname_np` for current thread only.
 * - Linux/glibc: `pthread_setname_np(pthread_self(), truncated15)`.
 */
[[nodiscard]] Expected<void> setThreadName(std::string_view name) noexcept;

/**
 * @brief Sets a `std::jthread`'s name by native handle.
 * @ingroup caudio_utils
 * @param jt Joinable jthread whose name to set.
 * @param name Non-empty name view.
 * @return Expected<void> success or error:
 * - `State` with "thread not joinable" if !jt.joinable(),
 * - `InvalidArg` if name empty,
 * - `Unsupported` if platform does not support naming this handle.
 * @details Implemented per-platform in `src/utils/thread.cpp`.
 */
[[nodiscard]] Expected<void> setThreadName(std::jthread& jt,
                                           std::string_view name) noexcept;

/**
 * @brief Sets a `std::thread`'s name by native handle.
 * @ingroup caudio_utils
 * @param t Joinable thread whose name to set.
 * @param name Non-empty name view.
 * @return Expected<void> success or error:
 * - `State` if !t.joinable(),
 * - `InvalidArg` if name empty,
 * - `Unsupported` if platform does not support naming this handle.
 * @details Implemented per-platform in `src/utils/thread.cpp`.
 */
[[nodiscard]] Expected<void> setThreadName(std::thread& t,
                                           std::string_view name) noexcept;

} // namespace caudio::utils
