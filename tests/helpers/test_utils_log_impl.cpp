#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>
import caudio.utils;

namespace caudio::utils::test {

bool log_injected() {
    std::vector<std::pair<LogLevel, std::string>> out;
    Logger logger(
        [&](LogLevel lvl, std::string_view msg) { out.emplace_back(lvl, std::string(msg)); });
    logger.log(LogLevel::Info, "hello {}", 42);
    if (out.size() != 1)
        return false;
    if (out[0].second.compare("hello 42") != 0)
        return false;
    if (out[0].first != LogLevel::Info)
        return false;
    return true;
}

bool log_level_filter() {
    std::vector<std::pair<LogLevel, std::string>> out;
    Logger logger([&](LogLevel lvl, std::string_view msg) { out.emplace_back(lvl, std::string(msg)); },
                  LogLevel::Warn);
    logger.log(LogLevel::Debug, "debug msg");
    logger.log(LogLevel::Info, "info msg");
    if (!out.empty())
        return false;
    logger.log(LogLevel::Warn, "warn msg");
    if (out.size() != 1)
        return false;
    if (out[0].first != LogLevel::Warn)
        return false;
    logger.log(LogLevel::Error, "error msg");
    if (out.size() != 2)
        return false;
    return true;
}

bool log_convenience() {
    std::vector<std::pair<LogLevel, std::string>> out;
    Logger logger(
        [&](LogLevel lvl, std::string_view msg) { out.emplace_back(lvl, std::string(msg)); });
    logger.debug("dbg {}", 1);
    logger.info("inf {}", 2);
    logger.warn("wrn {}", 3);
    logger.error("err {}", 4);
    if (out.size() != 4)
        return false;
    if (out[0].first != LogLevel::Debug)
        return false;
    if (out[0].second.compare("dbg 1") != 0)
        return false;
    if (out[1].first != LogLevel::Info)
        return false;
    if (out[2].first != LogLevel::Warn)
        return false;
    if (out[3].first != LogLevel::Error)
        return false;
    return true;
}

bool log_set_callback_level() {
    Logger logger;
    std::vector<std::string> out;
    logger.log(LogLevel::Info, "no cb yet");
    if (!out.empty())
        return false;
    logger.setCallback([&](LogLevel, std::string_view msg) { out.emplace_back(msg); });
    logger.log(LogLevel::Info, "now captured");
    if (out.size() != 1)
        return false;
    if (out[0].compare("now captured") != 0)
        return false;
    logger.setLevel(LogLevel::Error);
    logger.log(LogLevel::Warn, "filtered");
    if (out.size() != 1)
        return false;
    logger.log(LogLevel::Error, "passes");
    if (out.size() != 2)
        return false;
    return true;
}

bool log_null_safe() {
    Logger logger(nullptr);
    logger.log(LogLevel::Info, "no crash");
    logger.debug("also no crash");
    return true;
}

bool log_toString_level() {
    if (toString(LogLevel::Debug).compare("Debug") != 0)
        return false;
    if (toString(LogLevel::Info).compare("Info") != 0)
        return false;
    if (toString(LogLevel::Warn).compare("Warn") != 0)
        return false;
    if (toString(LogLevel::Error).compare("Error") != 0)
        return false;
    return true;
}

} // namespace caudio::utils::test






