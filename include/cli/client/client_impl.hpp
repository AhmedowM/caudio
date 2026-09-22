// TODO(Audit Directive 2, Appendix C §2.2): promote to include/caudio/client/client_impl.hpp — client SDK, not CLI-specific. Keep include/cli/client/client_impl.hpp as deprecated shim for one release: #include "caudio/client/client_impl.hpp".
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

#include "caudio/utils/utils.hpp"
#include "cli/client/ipc_client.hpp"
#include "cli/config.hpp"
#include "cli/service/ipc_channel.hpp"
#include "cli/service/shm_status.hpp"
#include "cli/shared/command.hpp"
#include "cli/shared/protocol.hpp"
#include "cli/shared/result.hpp"

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
