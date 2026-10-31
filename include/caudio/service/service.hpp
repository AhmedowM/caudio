#pragma once

/**
 * @file service.hpp
 * @brief Service: owns Engine, Database, Logger, IPC server, and dispatches
 * commands (daemon runtime used by the CLI service host).
 * @ingroup caudio_service
 * @details Command handlers, the internal dispatch visitor and component ownership live in
 * Service::Impl (defined in the non-installed src/service/service_impl.hpp),
 * so this header exposes only the public daemon surface.
 */

#include <caudio/config.hpp>
#include <caudio/fwd.hpp>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>

namespace caudio::service {

/**
 * @brief Configuration for Service creation.
 * @ingroup caudio_service
 */
struct ServiceConfig : public caudio::config::Config {
    /** @brief Echo log lines to stderr (foreground mode). Default: false. */
    bool logToStderr{false};
};

/**
 * @brief Main service class that owns core components and dispatches IPC commands.
 * @ingroup caudio_service
 *
 * Thread safety: Service is not thread-safe for concurrent method calls. The IPC server runs
 * on its own thread and invokes its internal dispatcher serially. External callers should not
 * invoke Service methods concurrently with server->run().
 *
 * Lifetime: Created via create(), runs via run(stop_token), cleaned up via shutdown() in
 * destructor. Uses RAII for all resources (DB, Engine, IPC server, PID file, lock file, shared
 * memory).
 */
class CAUDIO_API Service final {
  public:
    /**
     * @brief Result type returned by Service creation.
     * @ingroup caudio_service
     * @details Contains an owning service instance on success. On failure,
     * the error identifies configuration, single-instance, database, IPC,
     * or shared-memory initialization failure.
     */
    using ExpectedService = std::expected<std::unique_ptr<Service>, caudio::utils::Error>;

    /**
     * @brief Factory method to create and initialize a Service instance.
     * @ingroup caudio_service
     * @param cfg Service configuration (dbPath, optional socketPath, configPath, logLevel).
     * @param db Optional already-open database contract; when omitted, Service
     * opens its configured database.
     * @return Expected unique_ptr to Service, or Error if initialization fails.
     *
     * Performs single-instance enforcement via flock lock file and PID file checks.
     * Creates PID file, initializes shared memory status block for event-driven status snapshots,
     * and starts IPC server listening on derived or explicit socket path.
     */
    static ExpectedService create(const ServiceConfig& cfg,
                                  std::shared_ptr<caudio::contracts::IDatabase> db = nullptr);

    /**
     * @brief Destructor: ensures cleanup via shutdown().
     * @ingroup caudio_service
     */
    ~Service();

    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    Service(Service&&) = delete;
    Service& operator=(Service&&) = delete;

    /**
     * @brief Run the service event loop until stop_token is triggered.
     * @ingroup caudio_service
     * @param st Stop token to request graceful shutdown.
     * @return void on success, Error if already running.
     *
     * Starts the IPC server with an internal dispatcher for incoming commands.
     * Blocks until stop_token signals or shutdown() is called.
     * Thread safety: Must not be called concurrently with another run().
     * The token is observed between dispatch iterations; call `shutdown()` to
     * request immediate teardown from another thread.
     */
    caudio::utils::Expected<void> run(std::stop_token st);

    /**
     * @brief Replaces the attached database and reloads engine state from it.
     * @ingroup caudio_service
     * @param db Shared database contract (must be non-null and open).
     * @return Success or Error InvalidArg if db is null or not open.
     */
    caudio::utils::Expected<void> attachDatabase(std::shared_ptr<caudio::contracts::IDatabase> db);

    /**
     * @brief Gracefully shut down the service.
     * @ingroup caudio_service
     *
     * Signals shutdown to IPC server and Engine, removes PID file, socket file (Unix),
     * and releases the flock lock file. Shared memory handle is cleaned up via RAII.
     * Safe to call multiple times; idempotent.
     */
    void shutdown();

  private:
    /**
     * @brief Access the database instance.
     * @return Reference to the Database.
     */
    caudio::contracts::IDatabase& db() noexcept;

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
     * @brief Get the shared memory status writer (if available).
     * @return Pointer to ShmStatusWriter or nullptr if SHM creation failed.
     */
    caudio::ipc::ShmStatusWriter* shmWriter() noexcept;

    /**
     * @brief Private constructor used by create().
     */
    Service(const ServiceConfig& cfg, std::shared_ptr<caudio::contracts::IDatabase> db,
            std::unique_ptr<caudio::engine::Engine> eng, std::unique_ptr<IpcServer> srv,
            std::unique_ptr<caudio::utils::Logger> logger, std::filesystem::path pidPath,
            std::string socketPath, std::intptr_t lockFd,
            std::unique_ptr<caudio::ipc::ShmStatusWriter> shmWriter, std::string shmName);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace caudio::service
