#pragma once
/**
 * @file config.hpp
 * @brief Configuration management: canonical paths, load/save, and raw key/value access.
 * @ingroup caudio_config
 */

#include <array>
#include <caudio/utils.hpp>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace caudio::config {

/**
 * @brief Canonical path helpers: single source for socket/pid/lock derived from dbPath.
 * All three use hash of dbPath.generic_string() + XDG/LOCALAPPDATA base dir.
 * - Windows socket is Named Pipe \\.\pipe\caudio-<hex>, pid/lock are files under
 * %LOCALAPPDATA%\caudio
 * - POSIX socket/pid/lock are under $XDG_RUNTIME_DIR/caudio or $XDG_DATA_HOME/caudio or
 * ~/.local/share/caudio
 */
caudio::utils::Expected<std::string> socketPathFor(const std::filesystem::path& dbPath);
caudio::utils::Expected<std::filesystem::path> pidPathFor(const std::filesystem::path& dbPath);
caudio::utils::Expected<std::filesystem::path> lockPathFor(const std::filesystem::path& dbPath);

/**
 * @brief User-facing configuration loaded from config.json.
 * @ingroup caudio_config
 */
struct Config {
    /** @brief Database file path. Default: derived from XDG/LOCALAPPDATA. */
    std::filesystem::path dbPath{};
    /** @brief Config file path. Default: derived from XDG/LOCALAPPDATA. */
    std::filesystem::path configPath{};
    /** @brief Audio device ID ("auto" for default). */
    std::string device{"auto"};
    /** @brief Log level (0=trace,1=debug,2=info,3=warn,4=error). Default: 2 (info). */
    int logLevel{2};
    /** @brief Optional explicit socket path. If empty, derived from dbPath. */
    std::string socketPath{};
};


/**
 * @brief Load configuration from file with defaults.
 * @param path Config file path (empty = use default).
 * @return Config with defaults merged from file on success, Error on failure.
 *
 * Reads JSON config file, falls back to defaults for missing keys.
 * Supports legacy keys: db_path, log_level (snake_case).
 */
caudio::utils::Expected<Config> loadConfig(const std::filesystem::path& path);

/**
 * @brief Save configuration to file.
 * @param cfg Config to save.
 * @return void on success, Error on failure.
 *
 * Creates parent directories if needed. Writes JSON with 2-space indentation.
 * Only writes dbPath, device, logLevel, and socketPath (if non-empty).
 */
caudio::utils::Expected<void> saveConfig(const Config& cfg);



// Generic config raw access -- used by service for arbitrary key/value pairs.
// Implemented over caudio::utils::Json (single definition here, avoids per-module duplication).
struct RawConfigValue {
    std::string key{};
    std::string value{};
};

/**
 * @brief Get a raw config value by key from JSON file.
 * @param p Config file path.
 * @param key Key to read.
 * @return Value string (JSON string, "null", or JSON dump) on success, Error on failure.
 */
caudio::utils::Expected<std::string> configGetRaw(const std::filesystem::path& p,
                                                  std::string_view key);

/**
 * @brief Set a raw config value in JSON file.
 * Parses value as JSON if valid; otherwise stores as string.
 * Creates file and parent directories if needed.
 * @param p Config file path.
 * @param key Key to write.
 * @param value Value to write (JSON-parsed if valid JSON).
 * @return void on success, Error on failure.
 */
caudio::utils::Expected<void> configSetRaw(const std::filesystem::path& p, std::string_view key,
                                           std::string_view value);

/**
 * @brief List all config key-value pairs from JSON file.
 * Skips empty keys and "type" key.
 * @param p Config file path.
 * @return Vector of RawConfigValue on success, Error on failure.
 */
caudio::utils::Expected<std::vector<RawConfigValue>> configListRaw(const std::filesystem::path& p);

/**
 * @brief Delete a config key from JSON file.
 * @param p Config file path.
 * @param key Key to delete.
 * @return void on success, Error on failure (not found, corrupt, IO).
 */
caudio::utils::Expected<void> configDeleteRaw(const std::filesystem::path& p, std::string_view key);

/**
 * @brief Reset entire config (delete config file).
 * @param p Config file path.
 * @return void on success, Error on failure.
 */
caudio::utils::Expected<void> configResetAllRaw(const std::filesystem::path& p);

} // namespace caudio::config
