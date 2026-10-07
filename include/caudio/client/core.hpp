/**
 * @file core.hpp
 * @brief Client for the caudio daemon (used by the CLI and third-party frontends).
 * @ingroup caudio_client
 */
#pragma once

#include <caudio/utils/error.hpp>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

#ifndef _WIN32
#include <poll.h>
#endif

#include <caudio/config.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>

namespace caudio::client {

struct Config final {
    std::filesystem::path dbPath{"library.db"};
    std::string socketPath{};
};

class Client {
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

    caudio::utils::Expected<caudio::ipc::Result>
    send(const caudio::ipc::Command& cmd,
         std::chrono::milliseconds timeout = std::chrono::milliseconds{2000});

    // Snapshot status via shared memory (10fps status polling) or fallback to IPC
    caudio::utils::Expected<caudio::ipc::Result> snapshotStatus();
};

} // namespace caudio::client
