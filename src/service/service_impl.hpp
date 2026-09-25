// TODO(Audit Directive 2, Appendix C Ã‚Â§2.2): promote to include/caudio/service/service_impl.hpp Ã¢â‚¬â€ daemon runtime, not CLI-specific. Keep include/cli/service/service_impl.hpp as deprecated shim for one release: #include <caudio/service/service_impl.hpp>.
/**
 * @file service_impl.hpp
 * @brief Service implementation: owns Engine, Database, Logger, IPC server, and dispatches
 * commands.
 * @ingroup caudio_service
 */

#pragma once

// Ensure cli headers are included first to avoid windows.h conflicts
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <caudio/db.hpp>
#include <caudio/db/json.hpp>
#include <caudio/engine.hpp>
#include <caudio/utils.hpp>
#include <caudio/config.hpp>
#include <caudio/service/ipc_channel.hpp>
#include <caudio/service/ipc_server.hpp>
#include <caudio/service/service_detail.hpp>
#include <caudio/service/shm_status.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/protocol.hpp>
#include <caudio/ipc/result.hpp>

namespace caudio::service {

/**
 * @brief Configuration for Service creation.
 * @ingroup caudio_service
 */
struct ServiceConfig {
    /** @brief Path to SQLite database file. Defaults to "library.db" in current directory. */
    std::filesystem::path dbPath{"library.db"};
    /** @brief Optional explicit socket path. If empty, derived from dbPath via socketPathFor(). */
    std::string socketPath{};
    /** @brief Optional explicit config file path. If empty, derived from XDG/LOCALAPPDATA. */
    std::filesystem::path configPath{};
    /** @brief Log level (0=trace, 1=debug, 2=info, 3=warn, 4=error). Default: 2 (info). */
    int logLevel{2};
};

} // namespace caudio::service

namespace caudio::service {

/**
 * @brief Main service class that owns core components and dispatches IPC commands.
 * @ingroup caudio_service
 *
 * Thread safety: Service is not thread-safe for concurrent method calls. The IPC server runs
 * on its own thread and calls dispatch() serially. External callers should not invoke
 * Service methods concurrently with server->run().
 *
 * Lifetime: Created via create(), runs via run(stop_token), cleaned up via shutdown() in
 * destructor. Uses RAII for all resources (DB, Engine, IPC server, PID file, lock file, shared
 * memory).
 */
class Service final {
  public:
    using ExpectedService = std::expected<std::unique_ptr<Service>, caudio::utils::Error>;

    /**
     * @brief Factory method to create and initialize a Service instance.
     * @param cfg Service configuration (dbPath, optional socketPath, configPath, logLevel).
     * @return Expected unique_ptr to Service, or Error if initialization fails.
     *
     * Performs single-instance enforcement via flock lock file and PID file checks.
     * Creates PID file, initializes shared memory status block for TUI polling (10fps),
     * and starts IPC server listening on derived or explicit socket path.
     */
    static ExpectedService create(const ServiceConfig& cfg);

    /**
     * @brief Destructor: ensures cleanup via shutdown().
     */
    ~Service();

    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    Service(Service&&) = delete;
    Service& operator=(Service&&) = delete;

    /**
     * @brief Run the service event loop until stop_token is triggered.
     * @param st Stop token to request graceful shutdown.
     * @return void on success, Error if already running.
     *
     * Starts the IPC server with a dispatcher that forwards commands to dispatch().
     * Blocks until stop_token signals or shutdown() is called.
     * Thread safety: Must not be called concurrently with another run() or with dispatch().
     */
    caudio::utils::Expected<void> run(std::stop_token st);

    /**
     * @brief Gracefully shut down the service.
     *
     * Signals shutdown to IPC server and Engine, removes PID file, socket file (Unix),
     * and releases the flock lock file. Shared memory handle is cleaned up via RAII.
     * Safe to call multiple times; idempotent.
     */
    void shutdown();

    /**
     * @brief Access the database instance.
     * @return Reference to the Database.
     */
    caudio::db::Database& db() noexcept;

    /**
     * @brief Access the audio engine instance.
     * @return Reference to the Engine.
     */
    caudio::engine::Engine& engine() noexcept;

    /**
     * @brief Access the IPC server instance.
     * @return Reference to the IpcServer.
     */
    IpcServer& server() noexcept;

    /**
     * @brief Get the shared memory name used for status block.
     * @return Shared memory name (hash of dbPath).
     */
    const std::string& shmName() const noexcept;

    /**
     * @brief Get the shared memory status handle (if available).
     * @return Pointer to ShmStatusHandle or nullptr if SHM creation failed.
     */
    caudio::service::ShmStatusHandle* shmHandle() noexcept;

  private:
    /**
     * @brief Private constructor used by create().
     */
    Service(const ServiceConfig& cfg, std::shared_ptr<caudio::db::Database> db,
            std::unique_ptr<caudio::engine::Engine> eng, std::unique_ptr<IpcServer> srv,
            std::unique_ptr<caudio::utils::Logger> logger, std::filesystem::path pidPath,
            std::string socketPath, std::intptr_t lockFd,
            std::unique_ptr<caudio::service::ShmStatusHandle> shmHandle, std::string shmName);

    /**
     * @brief Update the shared memory status block with current engine state.
     *
     * Called after playback state changes (play, pause, seek, track change, queue modifications).
     * Updates track info, queue size, and duration in the SHM block for TUI polling at 10fps.
     * No-op if SHM handle is invalid or not created.
     */
    void updateShmStatus();

    /**
     * @brief Dispatch a command to the appropriate handler.
     * @param cmd Command variant to execute.
     * @return Result variant on success, Error on failure.
     *
     * Central command dispatcher using std::visit over the Command variant.
     * Each handler updates shared memory status after state changes.
     * Returns Status result for most commands to keep client state in sync.
     */
    std::expected<caudio::cli::Result, caudio::utils::Error>
    dispatch(const caudio::cli::Command& cmd);

    ServiceConfig config_;
    std::shared_ptr<caudio::db::Database> db_;
    std::unique_ptr<caudio::engine::Engine> engine_;
    std::unique_ptr<IpcServer> server_;
    std::unique_ptr<caudio::utils::Logger> logger_;
    std::filesystem::path pidPath_;
    std::string socketPath_;
    std::intptr_t lockFd_{-1};
    std::unique_ptr<caudio::service::ShmStatusHandle> shmHandle_;
    std::string shmName_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdownRequested_{false};
};

} // namespace caudio::service
