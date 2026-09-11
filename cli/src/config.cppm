module;
#include <array>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

export module caudio.cli:config;

import caudio.utils;
import caudio.json;

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
    int logLevel{2}; // 0=trace,1=debug,2=info,3=warn,4=error (default: info)
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

inline caudio::utils::Expected<std::string> readFileString(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "cannot open config")};
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return content;
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

    auto fileRes = detail::readFileString(cfgFile);
    if (!fileRes) return std::unexpected{fileRes.error()};
    std::string content = std::move(*fileRes);
    if (content.empty()) {
        return cfg;
    }
    try {
        auto j = caudio::json::ordered_json::parse(content);
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
        caudio::json::ordered_json j;
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
    return std::format("{:08x}", hv);
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

// Generic config raw access — used by service for arbitrary key/value pairs.
// Delegates to caudio::json::ordered_json (single definition here, avoids per-module duplication).
struct RawConfigValue {
    std::string key{};
    std::string value{};
};
inline caudio::utils::Expected<std::string> configGetRaw(const std::filesystem::path& p, std::string_view key);
inline caudio::utils::Expected<void> configSetRaw(const std::filesystem::path& p, std::string_view key, std::string_view value);
inline caudio::utils::Expected<std::vector<RawConfigValue>> configListRaw(const std::filesystem::path& p);

inline caudio::utils::Expected<std::string> configGetRaw(const std::filesystem::path& p, std::string_view key) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound, "config not found")};
    }
    auto fileRes = detail::readFileString(p);
    if (!fileRes) return std::unexpected{fileRes.error()};
    std::string content = std::move(*fileRes);
    if (content.empty()) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound, "key not found: " + std::string(key))};
    }
    try {
        auto j = caudio::json::ordered_json::parse(content);
        if (!j.is_object()) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, "config is not an object")};
        }
        std::string k(key);
        if (!j.contains(k)) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound, "key not found: " + k)};
        }
        auto& v = j.at(k);
        if (v.is_string()) return v.get<std::string>();
        if (v.is_null()) return std::string{"null"};
        return v.dump();
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, e.what())};
    }
}

inline caudio::utils::Expected<void> configSetRaw(const std::filesystem::path& p, std::string_view key, std::string_view value) {
    caudio::json::ordered_json j = caudio::json::ordered_json::object();
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) {
        auto fileRes = detail::readFileString(p);
        if (fileRes) {
            std::string content = std::move(*fileRes);
            if (!content.empty()) {
                try {
                    auto parsed = caudio::json::ordered_json::parse(content);
                    if (parsed.is_object()) j = std::move(parsed);
                    else j = caudio::json::ordered_json::object();
                } catch (...) {
                    j = caudio::json::ordered_json::object();
                }
            }
        }
    }
    std::string k(key);
    caudio::json::ordered_json v;
    bool parsedAsJson = false;
    if (!value.empty()) {
        try {
            auto tmp = caudio::json::ordered_json::parse(value);
            v = std::move(tmp);
            parsedAsJson = true;
        } catch (...) {
            parsedAsJson = false;
        }
    } else {
        v = std::string{};
        parsedAsJson = true;
    }
    if (!parsedAsJson) v = std::string(value);
    j[k] = std::move(v);
    try {
        auto parent = p.parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
        std::ofstream out(p);
        if (!out) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "cannot write config")};
        }
        out << j.dump(2);
        return {};
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, e.what())};
    }
}

inline caudio::utils::Expected<std::vector<RawConfigValue>> configListRaw(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return std::vector<RawConfigValue>{};
    auto fileRes = detail::readFileString(p);
    if (!fileRes) return std::unexpected{fileRes.error()};
    std::string content = std::move(*fileRes);
    if (content.empty()) return std::vector<RawConfigValue>{};
    try {
        auto j = caudio::json::ordered_json::parse(content);
        if (!j.is_object()) return std::vector<RawConfigValue>{};
        std::vector<RawConfigValue> out;
        out.reserve(j.size());
        for (auto& item : j.items()) {
            const std::string kk = item.key();
            auto& vv = item.value();
            if (kk.empty() || kk == "type") continue;
            std::string vs;
            if (vv.is_string()) vs = vv.get<std::string>();
            else if (vv.is_null()) vs = "null";
            else vs = vv.dump();
            out.push_back(RawConfigValue{kk, vs});
        }
        return out;
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, e.what())};
    }
}

} // namespace caudio::cli

