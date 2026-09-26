/**
 * @file client_impl.hpp
 * @brief Client implementation for caudio CLI.
 * @ingroup caudio_client
 */
#pragma once

#include <chrono>
#include <expected>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <poll.h>
#endif

#include <caudio/utils.hpp>
#include <caudio/client/ipc_client.hpp>
#include <caudio/config.hpp>
#include <caudio/service/ipc_channel.hpp>
#include <caudio/service/shm_status.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/protocol.hpp>
#include <caudio/ipc/result.hpp>

namespace caudio::client {

struct Config final {
    std::filesystem::path dbPath{"library.db"};
    std::string socketPath{};
};

class Client {
    Config config_;

  public:
    explicit Client(Config cfg);
    explicit Client(std::filesystem::path dbPath);
    explicit Client(std::filesystem::path dbPath, std::string_view socketPath);
    explicit Client(const caudio::cli::Config& cfg);

    const Config& config() const noexcept;
    std::filesystem::path dbPath() const noexcept;

    caudio::utils::Expected<caudio::cli::Result>
    send(const caudio::cli::Command& cmd,
         std::chrono::milliseconds timeout = std::chrono::milliseconds{2000});

    // Snapshot status via shared memory (for TUI 10fps polling) or fallback to IPC
    caudio::utils::Expected<caudio::cli::Result> snapshotStatus();
};

} // namespace caudio::client
