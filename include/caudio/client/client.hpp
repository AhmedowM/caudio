#pragma once

/**
 * @file client.hpp
 * @brief Client for the caudio daemon (used by the CLI and third-party frontends).
 * @ingroup caudio_client
 */

#include <caudio/config.hpp>
#include <caudio/fwd.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace caudio::client {

/**
 * @brief Transport view of the daemon location.
 * @ingroup caudio_client
 * @details Deliberately narrower than `caudio::config::Config`: only the
 * database and socket paths are needed to connect, and the `library.db`
 * default is part of this type's contract. Persisted configuration remains
 * `caudio::config::Config`.
 */
struct CAUDIO_API Config final {
    std::filesystem::path dbPath{"library.db"};
    std::string socketPath{};

    Config() = default;
    explicit Config(std::filesystem::path db) : dbPath(std::move(db)) {}
    Config(std::filesystem::path db, std::string sock)
        : dbPath(std::move(db)), socketPath(std::move(sock)) {}
    explicit Config(const caudio::config::Config& cfg)
        : dbPath(cfg.dbPath), socketPath(cfg.socketPath) {}
};

class CAUDIO_API Client {
    Config config_;

  public:
    /** @brief Constructs from an explicit config. */
    explicit Client(Config cfg);
    /** @brief Constructs for a database path (socket path derived). */
    explicit Client(std::filesystem::path dbPath);
    /** @brief Constructs for a database path with explicit socket path. */
    explicit Client(std::filesystem::path dbPath, std::string_view socketPath);
    /** @brief Constructs from a loaded app config. */
    explicit Client(const caudio::config::Config& cfg);

    const Config& config() const noexcept;
    std::filesystem::path dbPath() const noexcept;

    /**
     * @brief One-shot liveness check for the daemon at the configured paths.
     * @return True when a connection can be established; no command is sent.
     */
    bool probe() const;

    caudio::utils::Expected<caudio::ipc::Result>
    send(const caudio::ipc::Command& cmd,
         std::chrono::milliseconds timeout = std::chrono::milliseconds{2000});
};

} // namespace caudio::client
