module;
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

export module caudio.utils:log;

export namespace caudio::utils {

enum class Level : int { Debug = 0, Info = 1, Warn = 2, Error = 3 };

constexpr std::string_view toString(Level lvl) noexcept {
    switch (lvl) {
    case Level::Debug:
        return "Debug";
    case Level::Info:
        return "Info";
    case Level::Warn:
        return "Warn";
    case Level::Error:
        return "Error";
    default:
        return "Unknown";
    }
}

class Logger {
  public:
    using Callback = std::function<void(Level, std::string_view)>;

    explicit Logger(Callback cb = nullptr, Level minLevel = Level::Debug)
        : callback_(std::move(cb)), minLevel_(minLevel) {}

    void setCallback(Callback cb) {
        std::lock_guard<std::mutex> lk(mutex_);
        callback_ = std::move(cb);
    }

    void setLevel(Level lvl) noexcept {
        std::lock_guard<std::mutex> lk(mutex_);
        minLevel_ = lvl;
    }

    Level level() const noexcept {
        std::lock_guard<std::mutex> lk(mutex_);
        return minLevel_;
    }

    void log(Level lvl, std::string_view msg) {
        Callback cbCopy;
        Level minCopy;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!callback_)
                return;
            if (static_cast<int>(lvl) < static_cast<int>(minLevel_))
                return;
            cbCopy = callback_;
            minCopy = minLevel_;
            (void)minCopy;
        }
        cbCopy(lvl, msg);
    }

    template <typename... Args>
    void log(Level lvl, std::format_string<Args...> fmt, Args &&...args) {
        Callback cbCopy;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!callback_)
                return;
            if (static_cast<int>(lvl) < static_cast<int>(minLevel_))
                return;
            cbCopy = callback_;
        }
        if (!cbCopy)
            return;
        std::string s = std::format(fmt, std::forward<Args>(args)...);
        cbCopy(lvl, s);
    }

    template <typename... Args> void debug(std::format_string<Args...> fmt, Args &&...args) {
        log(Level::Debug, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args> void info(std::format_string<Args...> fmt, Args &&...args) {
        log(Level::Info, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args> void warn(std::format_string<Args...> fmt, Args &&...args) {
        log(Level::Warn, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args> void error(std::format_string<Args...> fmt, Args &&...args) {
        log(Level::Error, fmt, std::forward<Args>(args)...);
    }

    void debug(std::string_view msg) {
        log(Level::Debug, msg);
    }
    void info(std::string_view msg) {
        log(Level::Info, msg);
    }
    void warn(std::string_view msg) {
        log(Level::Warn, msg);
    }
    void error(std::string_view msg) {
        log(Level::Error, msg);
    }

  private:
    mutable std::mutex mutex_;
    Callback callback_;
    Level minLevel_{Level::Debug};
};

} // namespace caudio::utils
