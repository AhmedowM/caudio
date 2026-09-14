module;
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

/**
 * @file log.cppm
 * @brief Thread-safe logger with level filtering and format support.
 * @ingroup caudio_utils
 */

export module caudio.utils:log;

export namespace caudio::utils {

/**
 * @brief Log severity levels ordered low to high.
 * @ingroup caudio_utils
 * @details Filtering is `lvl >= minLevel`. Underlying values are used for
 * comparison via `std::to_underlying`.
 */
enum class LogLevel : int { Debug = 0, Info = 1, Warn = 2, Error = 3 };

/**
 * @brief Converts a LogLevel to a human-readable string view.
 * @ingroup caudio_utils
 * @param lvl Level to stringify.
 * @return Non-owning view; `"Unknown"` for out-of-range values.
 */
constexpr std::string_view toString(LogLevel lvl) noexcept {
    switch (lvl) {
    case LogLevel::Debug:
        return "Debug";
    case LogLevel::Info:
        return "Info";
    case LogLevel::Warn:
        return "Warn";
    case LogLevel::Error:
        return "Error";
    default:
        return "Unknown";
    }
}

/**
 * @brief Thread-safe logger with callback and level filtering.
 * @ingroup caudio_utils
 * @details Lock ordering: a single `mutable std::mutex` protects both
 * `callback_` and `minLevel_`. All mutators (`setCallback`, `setLevel`)
 * and observers (`level`, `getCallbackIfNeeded`) lock the mutex. `log()`
 * copies the callback under lock then invokes outside the lock to avoid
 * deadlocks if the callback re-enters Logger.
 *
 * Thread-safety: concurrent `log`/`setCallback`/`setLevel` is safe.
 * Callback invocation is not synchronized beyond the copy; callback must
 * be thread-safe if it touches shared state.
 *
 * @see LogLevel
 * @see toString
 */
class Logger {
  public:
    /**
     * @brief Callback invoked for each enabled log message.
     * @ingroup caudio_utils
     * @param level Severity of the message.
     * @param message Non-owning view of the log text (valid for call duration).
     */
    using Callback = std::function<void(LogLevel, std::string_view)>;

    /**
     * @brief Constructs a logger.
     * @ingroup caudio_utils
     * @param cb Optional callback; null means messages are dropped.
     * @param minLevel Minimum level to emit (default Debug = all).
     */
    explicit Logger(Callback cb = nullptr, LogLevel minLevel = LogLevel::Debug)
        : callback_(std::move(cb)), minLevel_(minLevel) {}

    /**
     * @brief Replaces the callback atomically.
     * @ingroup caudio_utils
     * @param cb New callback (may be null to disable logging).
     * @details Locks mutex_, moves callback in.
     */
    void setCallback(Callback cb) {
        std::lock_guard<std::mutex> lk(mutex_);
        callback_ = std::move(cb);
    }

    /**
     * @brief Sets the minimum level to emit.
     * @ingroup caudio_utils
     * @param lvl New minimum; messages with `lvl < minLevel` are dropped.
     * @details Locks mutex_.
     */
    void setLevel(LogLevel lvl) noexcept {
        std::lock_guard<std::mutex> lk(mutex_);
        minLevel_ = lvl;
    }

    /**
     * @brief Returns the current minimum level.
     * @ingroup caudio_utils
     * @return Current minLevel (copy under lock).
     */
    [[nodiscard]] LogLevel level() const noexcept {
        std::lock_guard<std::mutex> lk(mutex_);
        return minLevel_;
    }

    /**
     * @brief Emits a pre-formatted message if enabled.
     * @ingroup caudio_utils
     * @param lvl Severity of the message.
     * @param msg Message view (forwarded to callback verbatim).
     * @details Copies callback under lock via getCallbackIfNeeded(), returns
     * early if no callback or filtered by minLevel. Invocation happens
     * outside the lock.
     */
    void log(LogLevel lvl, std::string_view msg) {
        Callback cbCopy;
        if (bool ok = getCallbackIfNeeded(lvl, cbCopy); !ok)
            return;
        cbCopy(lvl, msg);
    }

    /**
     * @brief Emits a formatted message if enabled.
     * @ingroup caudio_utils
     * @tparam Args Format argument types.
     * @param lvl Severity.
     * @param fmt Compile-time format string (`std::format_string`).
     * @param args Arguments forwarded to `std::format`.
     * @details Filtering and callback copy happen under lock before
     * formatting. Formatting is done outside the lock to minimize hold time.
     */
    template <typename... Args>
    void log(LogLevel lvl, std::format_string<Args...> fmt, Args&&... args) {
        Callback cbCopy;
        if (bool ok = getCallbackIfNeeded(lvl, cbCopy); !ok)
            return;
        std::string s = std::format(fmt, std::forward<Args>(args)...);
        cbCopy(lvl, s);
    }

    /**
     * @brief Emits a formatted Debug message.
     * @ingroup caudio_utils
     * @tparam Args Format argument types.
     * @param fmt Format string.
     * @param args Format arguments.
     */
    template <typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    /**
     * @brief Emits a formatted Info message.
     * @ingroup caudio_utils
     * @tparam Args Format argument types.
     * @param fmt Format string.
     * @param args Format arguments.
     */
    template <typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    /**
     * @brief Emits a formatted Warn message.
     * @ingroup caudio_utils
     * @tparam Args Format argument types.
     * @param fmt Format string.
     * @param args Format arguments.
     */
    template <typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Warn, fmt, std::forward<Args>(args)...);
    }
    /**
     * @brief Emits a formatted Error message.
     * @ingroup caudio_utils
     * @tparam Args Format argument types.
     * @param fmt Format string.
     * @param args Format arguments.
     */
    template <typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief Emits a Debug message view.
     * @ingroup caudio_utils
     * @param msg Message view.
     */
    void debug(std::string_view msg) {
        log(LogLevel::Debug, msg);
    }
    /**
     * @brief Emits an Info message view.
     * @ingroup caudio_utils
     * @param msg Message view.
     */
    void info(std::string_view msg) {
        log(LogLevel::Info, msg);
    }
    /**
     * @brief Emits a Warn message view.
     * @ingroup caudio_utils
     * @param msg Message view.
     */
    void warn(std::string_view msg) {
        log(LogLevel::Warn, msg);
    }
    /**
     * @brief Emits an Error message view.
     * @ingroup caudio_utils
     * @param msg Message view.
     */
    void error(std::string_view msg) {
        log(LogLevel::Error, msg);
    }

  private:
    /**
     * @brief Checks if logging is enabled and copies callback.
     * @param lvl Message level to test.
     * @param out Receives callback copy if enabled.
     * @return true if callback present and lvl >= minLevel.
     * @details Locking: acquires mutex_, checks callback and level filter
     * via `std::to_underlying`, copies callback to `out`. Caller invokes
     * outside the lock.
     */
    bool getCallbackIfNeeded(LogLevel lvl, Callback& out) {
        std::lock_guard lk(mutex_);
        if (!callback_)
            return false;
        if (std::to_underlying(lvl) < std::to_underlying(minLevel_))
            return false;
        out = callback_;
        return true;
    }

    mutable std::mutex mutex_;         ///< Protects callback_ and minLevel_.
    Callback callback_;                ///< User-provided sink; null = disabled.
    LogLevel minLevel_{LogLevel::Debug}; ///< Minimum level to emit.
};

} // namespace caudio::utils
