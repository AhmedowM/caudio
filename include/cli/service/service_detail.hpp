#pragma once

/**
 * @file service_detail.hpp
 * @brief Internal helpers for caudio.service — not exported. Contains path utilities,
 * locking, PID management, SHM status building, config helpers, and audio file utilities.
 * @ingroup caudio_service
 */

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "blake3.h"

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#else
#include <process.h>
// Do NOT include windows.h here — causes HMODULE/HANDLE conflicts with caudio::utils
// Windows-specific API usage is in the .cpp with proper extern "C" declarations.
#endif

#include "caudio/utils/utils.hpp"
#include "caudio/engine/engine.hpp"
#include "caudio/db/database.hpp"
#include "cli/cli.hpp"
#include "caudio/player/player.hpp"

namespace caudio::service::detail {

/**
 * @brief Overload set for std::visit with multiple lambdas.
 * @tparam Ts Callable types.
 */
template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

/**
 * @brief Get PID file path for a database path.
 * Delegates to cli::pidPathFor; falls back to dbPath parent / "caudio.pid".
 * @param dbPath Database path.
 * @param socketPath Unused (kept for signature compatibility).
 * @return PID file path.
 */
std::filesystem::path pidPathForSocket(const std::filesystem::path& dbPath,
                                       const std::string& /*socketPath*/);

/**
 * @brief Get lock file path for a database path.
 * Delegates to cli::lockPathFor; falls back to PID dir / "caudio-<hash>.lock".
 * @param dbPath Database path.
 * @param socketPath Unused (kept for signature compatibility).
 * @return Lock file path.
 */
std::filesystem::path lockPathForSocket(const std::filesystem::path& dbPath,
                                        const std::string& /*socketPath*/);

/**
 * @brief Get socket path for a database path.
 * Delegates to cli::socketPathFor; falls back to platform-specific default.
 * Windows: Named pipe \\.\pipe\caudio-<hash>
 * POSIX: $XDG_RUNTIME_DIR/caudio/caudio-<hash>.sock (created if needed)
 * @param dbPath Database path.
 * @return Socket path string.
 */
std::string socketPathForDb(const std::filesystem::path& dbPath);

/**
 * @brief Probe if a socket/named pipe is alive (connectable).
 * Windows: Try CreateFileW + WaitNamedPipeW.
 * POSIX: Try connect() to AF_UNIX socket.
 * @param sp Socket path string.
 * @return true if connection succeeds (service alive), false otherwise.
 */
bool probeSocketAlive(const std::string& sp);

/**
 * @brief Try to acquire exclusive lock on a file (single-instance enforcement).
 * Windows: CreateFileW + LockFileEx on first byte.
 * POSIX: open() + flock(LOCK_EX | LOCK_NB).
 * Creates parent directories if needed.
 * @param lockPath Lock file path.
 * @param outFd Output file descriptor/handle (set on success).
 * @return true if lock acquired, false if already held or error.
 */
bool tryAcquireLock(const std::filesystem::path& lockPath, int& outFd);

/**
 * @brief Release a previously acquired lock.
 * Windows: UnlockFileEx + CloseHandle.
 * POSIX: flock(LOCK_UN) + close().
 * @param fd File descriptor/handle returned by tryAcquireLock.
 */
void releaseLock(int fd);

/**
 * @brief Check if a process ID is alive.
 * POSIX: kill(pid, 0) == 0.
 * Windows: OpenProcess(SYNCHRONIZE) + WaitForSingleObject(0) == WAIT_TIMEOUT.
 * @param pid Process ID to check.
 * @return true if process exists, false otherwise.
 */
bool checkPidAlive(int pid);

/**
 * @brief Read PID from a PID file.
 * @param pidPath Path to PID file.
 * @return PID if file exists and contains valid integer, nullopt otherwise.
 */
std::optional<int> readPidFile(const std::filesystem::path& pidPath);

/**
 * @brief Build a Status object from Engine and Database.
 * Populates playback state, position, duration, volume, shuffle, repeat,
 * current track info (title, artist, path), and active queue size/index.
 * @param eng Engine reference.
 * @param db Database reference.
 * @return Status on success, Error on failure.
 */
std::expected<caudio::cli::Status, caudio::utils::Error>
buildStatus(caudio::engine::Engine& eng, caudio::db::Database& db);

/**
 * @brief Resolve config file path.
 * Priority: explicit configPath > dbPath parent / "config.json" > temp/caudio/config.json.
 * @param configPath Explicit config path (may be empty).
 * @param dbPath Database path.
 * @return Resolved config file path.
 */
std::filesystem::path resolveConfigPath(const std::filesystem::path& configPath,
                                        const std::filesystem::path& dbPath);

/**
 * @brief Read a single config value from JSON file.
 * Delegates to cli::configGetRaw.
 * @param p Config file path.
 * @param key Config key to read.
 * @return Value string on success, Error on failure.
 */
std::expected<std::string, caudio::utils::Error>
readConfigValueRaw(const std::filesystem::path& p, std::string_view key);

/**
 * @brief Write a single config value to JSON file.
 * Delegates to cli::configSetRaw.
 * @param p Config file path.
 * @param key Config key to write.
 * @param value Value to write (JSON-parsed if valid JSON, else string).
 * @return void on success, Error on failure.
 */
caudio::utils::Expected<void>
writeConfigValueRaw(const std::filesystem::path& p, std::string_view key, std::string_view value);

/**
 * @brief List all config key-value pairs from JSON file.
 * Delegates to cli::configListRaw.
 * @param p Config file path.
 * @return Vector of ConfigValue on success, Error on failure.
 */
std::expected<std::vector<caudio::cli::ConfigValue>, caudio::utils::Error>
listConfigValuesRaw(const std::filesystem::path& p);

/**
 * @brief Delete a config key from JSON file.
 * Delegates to cli::configDeleteRaw.
 * @param p Config file path.
 * @param key Config key to delete.
 * @return void on success, Error on failure.
 */
caudio::utils::Expected<void>
deleteConfigValueRaw(const std::filesystem::path& p, std::string_view key);

/**
 * @brief Reset entire config file (delete it).
 * Delegates to cli::configResetAllRaw.
 * @param p Config file path.
 * @return void on success, Error on failure.
 */
caudio::utils::Expected<void> resetAllConfigRaw(const std::filesystem::path& p);

/**
 * @brief Check if a file has a supported audio extension.
 * Supported: .mp3, .flac, .ogg, .wav, .m4a (case-insensitive).
 * @param p File path.
 * @return true if extension matches, false otherwise.
 */
bool hasAudioExt(const std::filesystem::path& p);

/**
 * @brief Compute BLAKE3 fingerprint of an audio file.
 * Hashes first 64KB, last 64KB (if file larger), plus file size and version.
 * @param path Audio file path.
 * @return 32-byte fingerprint array on success, Error on failure.
 */
std::expected<std::array<std::uint8_t, 32>, caudio::utils::Error>
computeFingerprint(const std::filesystem::path& path);

/**
 * @brief Get audio duration from decoder.
 * Opens file reader and decoder to read sample rate and total frames.
 * @param path Audio file path.
 * @return Duration in seconds, or 0.0 on failure.
 */
double durationFromDecoder(const std::filesystem::path& path) noexcept;

} // namespace caudio::service::detail