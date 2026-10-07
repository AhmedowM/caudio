#pragma once

#include <caudio/config.hpp>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

/**
 * @file app.hpp
 * @brief Application orchestrator shared by all frontends.
 * @ingroup caudio_app
 * @details `App` owns the behavior moved out of the CLI shell: daemon
 * lifecycle (spawn, readiness, shutdown), transport policy (autostart,
 * retries) and, over time, every command dispatch path. Frontends (the CLI
 * shell today, TUIs tomorrow) keep only their own syntax and rendering and
 * call into this class.
 *
 * Phase 0 note: rendering still happens inside these methods (moved verbatim
 * from the shell); Phase 1 converts them to return data for callers to
 * render. Exit-code convention is CLI-compatible throughout: 0 ok, 1
 * runtime failure.
 *
 * Thread safety: thread-safe for concurrent calls; daemon interactions go
 * through the IPC client, which serializes per connection.
 */
namespace caudio::app {

/**
 * @brief Application orchestrator bound to one config (one database).
 * @ingroup caudio_app
 * @see caudio::config::Config
 */
class App {
  public:
    /**
     * @brief Binds the orchestrator to a config.
     * @ingroup caudio_app
     * @param cfg Resolved config (db/socket/log paths); copied.
     */
    explicit App(caudio::config::Config cfg);
    /** @brief Dtor. @ingroup caudio_app */
    ~App();

    /**
     * @brief Replaces the bound config (after frontend flag parsing).
     * @ingroup caudio_app
     * @param cfg Fully resolved config; copied.
     * @details Frontends construct App before parsing their own flags, so
     * the bound config is stale until they call this once parsing plus
     * config-file reload plus socket derivation are done. Every daemon
     * interaction below must observe the final config.
     */
    void setConfig(caudio::config::Config cfg);

    /**
     * @brief Sets argv[0] for daemon re-spawn path resolution.
     * @ingroup caudio_app
     * @param argv0 Executable path as invoked (POSIX fallback only).
     * @details Frontends call this once from their entry point before any
     * spawn; on Windows the module path is used instead.
     */
    void setArgv0(std::string argv0);

    /**
     * @brief Starts the daemon (foreground service or background spawn).
     * @ingroup caudio_app
     * @param foreground Run the service in-process and block (child path).
     * @param quiet Suppress informational output (JSON/autostart callers).
     * @return 0 when the daemon answers a status round-trip, 1 otherwise.
     * @details Background path waits out a previous daemon's teardown,
     * spawns once, then requires a full StatusReq round-trip (not just a
     * connect) within budget before reporting success.
     */
    int startDaemon(bool foreground, bool quiet = false);

    /**
     * @brief Stops the daemon and waits for its teardown to complete.
     * @ingroup caudio_app
     * @return 0 when the daemon is unreachable and its pid file is gone.
     */
    int shutdownDaemon();

    /**
     * @brief Spawns a detached daemon child for the given config.
     * @ingroup caudio_app
     * @param cfg Config selecting db/socket paths for the child.
     * @return Success, or the OS error code when spawning failed.
     * @details Windows uses DETACHED_PROCESS; POSIX double-detaches via
     * fork/setsid (single fork + wait-free, like the historical behavior).
     * The child's readiness is NOT waited on here -- see startDaemon().
     */
    std::expected<void, std::uint32_t> spawnDaemon(const caudio::config::Config& cfg);

    /**
     * @brief Derives the pid-file path for the bound config.
     * @ingroup caudio_app
     * @return Canonical pid path, or a db-adjacent fallback.
     */
    std::filesystem::path pidPathForConfig() const;

  private:
    caudio::config::Config config_; ///< Bound config (db/socket/log paths).
    std::string argv0_;             ///< Executable path for POSIX re-spawn.
};

} // namespace caudio::app
