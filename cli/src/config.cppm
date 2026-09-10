module;
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

export module caudio.cli:config;

import caudio.utils;

export namespace caudio::cli {

// Canonical path helpers: single source for socket/pid/lock derived from dbPath.
// All three use hash of dbPath.generic_string() + XDG/LOCALAPPDATA base dir.
// - Windows socket is Named Pipe \\.\pipe\caudio-<hex>, pid/lock are files under %LOCALAPPDATA%\caudio
// - POSIX socket/pid/lock are under $XDG_RUNTIME_DIR/caudio or $XDG_DATA_HOME/caudio or ~/.local/share/caudio
inline caudio::utils::Expected<std::string> socketPathFor(const std::filesystem::path& dbPath);
inline caudio::utils::Expected<std::filesystem::path> pidPathFor(const std::filesystem::path& dbPath);
inline caudio::utils::Expected<std::filesystem::path> lockPathFor(const std::filesystem::path& dbPath);

struct Config {
    std::filesystem::path dbPath{};
    std::filesystem::path configPath{};
    std::string device{"auto"};
    int logLevel{2};
    std::string socketPath{};
};

namespace detail {

inline std::filesystem::path defaultDbPath() {
#ifdef _WIN32
    const char* localApp = std::getenv("LOCALAPPDATA");
    if (localApp && localApp[0] != '\0') {
        return std::filesystem::path(localApp) / "caudio" / "library.db";
    }
#endif
    const char* xdgData = std::getenv("XDG_DATA_HOME");
    std::filesystem::path base;
    if (xdgData && xdgData[0] != '\0') {
        base = std::filesystem::path(xdgData) / "caudio";
    } else {
        const char* home = std::getenv("HOME");
        if (!home || home[0] == '\0') home = std::getenv("USERPROFILE");
        if (home && home[0] != '\0') {
            base = std::filesystem::path(home) / ".local" / "share" / "caudio";
        } else {
            std::error_code ec;
            base = std::filesystem::temp_directory_path(ec) / "caudio";
            if (ec) base = std::filesystem::path("/tmp/caudio");
        }
    }
    return base / "library.db";
}

inline std::filesystem::path defaultConfigPath() {
    const char* xdgCfg = std::getenv("XDG_CONFIG_HOME");
    std::filesystem::path base;
    if (xdgCfg && xdgCfg[0] != '\0') {
        base = std::filesystem::path(xdgCfg) / "caudio";
    } else {
        const char* home = std::getenv("HOME");
        if (!home || home[0] == '\0') home = std::getenv("USERPROFILE");
        if (home && home[0] != '\0') {
            base = std::filesystem::path(home) / ".config" / "caudio";
        } else {
            std::error_code ec;
            base = std::filesystem::temp_directory_path(ec) / "caudio";
            if (ec) base = std::filesystem::path("/tmp/caudio");
        }
    }
    return base / "config.json";
}

} // namespace detail

inline caudio::utils::Expected<Config> loadConfig(const std::filesystem::path& path) {
    Config cfg{};
    cfg.dbPath = detail::defaultDbPath();
    cfg.configPath = path.empty() ? detail::defaultConfigPath() : path;
    cfg.device = "auto";
    cfg.logLevel = 2;

    std::filesystem::path cfgFile = cfg.configPath;
    std::error_code ec;
    if (!std::filesystem::exists(cfgFile, ec)) {
        // no config file -> return defaults
        return cfg;
    }

    std::ifstream in(cfgFile);
    if (!in) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "cannot open config")};
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.empty()) {
        return cfg;
    }
    try {
        auto j = nlohmann::ordered_json::parse(content);
        if (j.contains("dbPath") && j["dbPath"].is_string()) {
            std::string s = j["dbPath"].get<std::string>();
            if (!s.empty()) cfg.dbPath = std::filesystem::path(s);
        }
        if (j.contains("configPath") && j["configPath"].is_string()) {
            // ignore - already set
        }
        if (j.contains("device") && j["device"].is_string()) {
            cfg.device = j["device"].get<std::string>();
        }
        if (j.contains("logLevel") && j["logLevel"].is_number_integer()) {
            cfg.logLevel = j["logLevel"].get<int>();
        }
        if (j.contains("socketPath") && j["socketPath"].is_string()) {
            std::string s = j["socketPath"].get<std::string>();
            if (!s.empty()) cfg.socketPath = s;
        }
        // legacy keys: db_path, log_level
        if (j.contains("db_path") && j["db_path"].is_string()) {
            std::string s = j["db_path"].get<std::string>();
            if (!s.empty()) cfg.dbPath = std::filesystem::path(s);
        }
        if (j.contains("log_level") && j["log_level"].is_number_integer()) {
            cfg.logLevel = j["log_level"].get<int>();
        }
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, e.what())};
    }
    return cfg;
}

inline caudio::utils::Expected<void> saveConfig(const Config& cfg) {
    try {
        std::filesystem::path dir = cfg.configPath.parent_path();
        if (!dir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
        }
        nlohmann::ordered_json j;
        j["dbPath"] = cfg.dbPath.generic_string();
        j["device"] = cfg.device;
        j["logLevel"] = cfg.logLevel;
        if (!cfg.socketPath.empty()) j["socketPath"] = cfg.socketPath;
        std::ofstream out(cfg.configPath);
        if (!out) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "cannot write config")};
        }
        out << j.dump(2);
        return {};
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, e.what())};
    }
}

// Canonical socket/pid/lock path helpers — single source, XDG/LOCALAPPDATA + hash(dbPath.generic_string())
namespace detail_paths {
inline std::string hex8ForDb(const std::filesystem::path& dbPath) {
    std::string input = dbPath.generic_string();
    if (input.empty()) input = dbPath.string();
    std::size_t raw = std::hash<std::string>{}(input);
    std::uint32_t hv = static_cast<std::uint32_t>(raw & 0xFFFFFFFFu);
    hv ^= static_cast<std::uint32_t>((raw >> 32) & 0xFFFFFFFFu);
    constexpr char kHex[] = "0123456789abcdef";
    std::array<char, 9> buf{};
    for (int i = 7; i >= 0; --i) {
        buf[static_cast<std::size_t>(i)] = kHex[hv & 0xFu];
        hv >>= 4;
    }
    return std::string(buf.data(), 8);
}
inline std::filesystem::path baseDirForSocket() {
#ifdef _WIN32
    const char* localApp = std::getenv("LOCALAPPDATA");
    if (localApp && localApp[0] != '\0') {
        return std::filesystem::path(localApp) / "caudio";
    }
#endif
    const char* xdgRuntime = std::getenv("XDG_RUNTIME_DIR");
    if (xdgRuntime && xdgRuntime[0] != '\0') {
        return std::filesystem::path(xdgRuntime) / "caudio";
    }
    const char* xdgData = std::getenv("XDG_DATA_HOME");
    if (xdgData && xdgData[0] != '\0') {
        return std::filesystem::path(xdgData) / "caudio";
    }
    const char* home = std::getenv("HOME");
    if (!home || home[0] == '\0') home = std::getenv("USERPROFILE");
    if (home && home[0] != '\0') {
        return std::filesystem::path(home) / ".local" / "share" / "caudio";
    }
    std::error_code ec;
    auto base = std::filesystem::temp_directory_path(ec) / "caudio";
    if (ec) base = std::filesystem::path("/tmp/caudio");
    return base;
}
} // namespace detail_paths

inline caudio::utils::Expected<std::string> socketPathFor(const std::filesystem::path& dbPath) {
    try {
        std::string hex = detail_paths::hex8ForDb(dbPath);
#ifdef _WIN32
        return std::string("\\\\.\\pipe\\caudio-") + hex;
#else
        auto base = detail_paths::baseDirForSocket();
        return (base / ("caudio-" + hex + ".sock")).generic_string();
#endif
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, e.what())};
    } catch (...) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "socketPathFor failed")};
    }
}

inline caudio::utils::Expected<std::filesystem::path> pidPathFor(const std::filesystem::path& dbPath) {
    try {
        std::string hex = detail_paths::hex8ForDb(dbPath);
        auto base = detail_paths::baseDirForSocket();
        return base / ("caudio-" + hex + ".pid");
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, e.what())};
    } catch (...) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "pidPathFor failed")};
    }
}

inline caudio::utils::Expected<std::filesystem::path> lockPathFor(const std::filesystem::path& dbPath) {
    try {
        std::string hex = detail_paths::hex8ForDb(dbPath);
        auto base = detail_paths::baseDirForSocket();
        return base / ("caudio-" + hex + ".lock");
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, e.what())};
    } catch (...) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "lockPathFor failed")};
    }
}

} // namespace caudio::cli
