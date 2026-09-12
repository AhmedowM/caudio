module;
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

export module caudio.utils:log;

export namespace caudio::utils {

enum class LogLevel : int { Debug = 0, Info = 1, Warn = 2, Error = 3 };

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

class Logger {
  public:
    using Callback = std::function<void(LogLevel, std::string_view)>;

    explicit Logger(Callback cb = nullptr, LogLevel minLevel = LogLevel::Debug)
        : callback_(std::move(cb)), minLevel_(minLevel) {}

    void setCallback(Callback cb) {
        std::lock_guard<std::mutex> lk(mutex_);
        callback_ = std::move(cb);
    }

    void setLevel(LogLevel lvl) noexcept {
        std::lock_guard<std::mutex> lk(mutex_);
        minLevel_ = lvl;
    }

    [[nodiscard]] LogLevel level() const noexcept {
        std::lock_guard<std::mutex> lk(mutex_);
        return minLevel_;
    }

    void log(LogLevel lvl, std::string_view msg) {
        Callback cbCopy;
        if (bool ok = getCallbackIfNeeded(lvl, cbCopy); !ok)
            return;
        cbCopy(lvl, msg);
    }

    template <typename... Args>
    void log(LogLevel lvl, std::format_string<Args...> fmt, Args&&... args) {
        Callback cbCopy;
        if (bool ok = getCallbackIfNeeded(lvl, cbCopy); !ok)
            return;
        std::string s = std::format(fmt, std::forward<Args>(args)...);
        cbCopy(lvl, s);
    }

    template <typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Warn, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) {
        log(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }

    void debug(std::string_view msg) {
        log(LogLevel::Debug, msg);
    }
    void info(std::string_view msg) {
        log(LogLevel::Info, msg);
    }
    void warn(std::string_view msg) {
        log(LogLevel::Warn, msg);
    }
    void error(std::string_view msg) {
        log(LogLevel::Error, msg);
    }

  private:
    bool getCallbackIfNeeded(LogLevel lvl, Callback& out) {
        std::lock_guard lk(mutex_);
        if (!callback_)
            return false;
        if (std::to_underlying(lvl) < std::to_underlying(minLevel_))
            return false;
        out = callback_;
        return true;
    }

    mutable std::mutex mutex_;
    Callback callback_;
    LogLevel minLevel_{LogLevel::Debug};
};

} // namespace caudio::utils
