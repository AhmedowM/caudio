#pragma once

/**
 * @file config_detail.hpp
 * @brief Internal path helpers for config (not public API).
 * @ingroup caudio_config
 */

#include <filesystem>
#include <string>

#include <caudio/utils.hpp>

namespace caudio::config::detail {

/**
 * @brief Get default database path.
 * Windows: %LOCALAPPDATA%\caudio\library.db
 * POSIX: $XDG_DATA_HOME/caudio/library.db or ~/.local/share/caudio/library.db
 * @return Default database path.
 */
std::filesystem::path defaultDbPath();

/**
 * @brief Get default config file path.
 * Windows: %LOCALAPPDATA%\caudio\config.json (same as db dir)
 * POSIX: $XDG_CONFIG_HOME/caudio/config.json or ~/.config/caudio/config.json
 * @return Default config file path.
 */
std::filesystem::path defaultConfigPath();

/**
 * @brief Read entire file into string.
 * @param p File path.
 * @return File contents on success, Error on failure.
 */
caudio::utils::Expected<std::string> readFileString(const std::filesystem::path& p);

} // namespace detail

namespace caudio::config::detail_paths {

/**
 * @brief Compute 8-char hex hash from dbPath for socket/pid/lock naming.
 * Uses std::hash on dbPath.generic_string(), folded to 32 bits, formatted as 8-char hex.
 * @param dbPath Database path.
 * @return 8-character lowercase hex string.
 */
std::string hex8ForDb(const std::filesystem::path& dbPath);

/**
 * @brief Get base directory for socket/pid/lock files.
 * Windows: %LOCALAPPDATA%\caudio
 * POSIX: $XDG_RUNTIME_DIR/caudio > $XDG_DATA_HOME/caudio > ~/.local/share/caudio > temp/caudio
 * @return Base directory path.
 */
std::filesystem::path baseDirForSocket();
} // namespace detail_paths
