module;
#include <nlohmann/json.hpp>

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

} // namespace caudio::cli
