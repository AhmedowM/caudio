#pragma once

/**
 * @file ipc_server.hpp
 * @brief Accept-loop IPC server (daemon side).
 * @ingroup caudio_service
 * @details Listens on a Unix domain socket (POSIX) or named pipe (Windows)
 * and dispatches each connection to the `Service` dispatcher on its own
 * thread. Windows: local API declarations are used when `windows.h` was
 * not included yet, so including this header never forces `windows.h`
 * (and its `min`/`max` macros) on consumers; see `shm_status.hpp` and
 * `utils/thread.hpp` for the same pattern.
 */

#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace caudio::service {

/**
 * @brief Daemon-side accept loop; one thread per connection.
 * @ingroup caudio_service
 */
class IpcServer {
  public:
    /** @brief Default-constructs an idle server (call listen() next). */
    IpcServer() = default;
    /** @brief Shuts down and joins threads. */
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;
    IpcServer(IpcServer&&) = delete;
    IpcServer& operator=(IpcServer&&) = delete;

    /**
     * @brief Binds the socket/pipe derived from the database path.
     * @ingroup caudio_service
     * @param dbPath Database path (socket path is derived from it).
     * @param socketPathOverride Explicit path, bypassing derivation.
     * @return Success or `Io` Error (address in use, permission).
     */
    caudio::utils::Expected<void> listen(const std::filesystem::path& dbPath,
                                         std::string_view socketPathOverride = {});

    /**
     * @brief Serves connections until the stop token fires.
     * @ingroup caudio_service
     * @param st Stop token ending the accept loop.
     * @param dispatch Called per request on a connection thread.
     */
    void run(std::stop_token st,
             std::function<std::expected<caudio::ipc::Result, caudio::utils::Error>(
                 const caudio::ipc::Command&)>
                 dispatch);

    /**
     * @brief Stops accepting and joins connection threads (idempotent).
     * @ingroup caudio_service
     */
    void shutdown();

    /** @brief Marks the calling connection thread done (self-retire). */
    void retireClient();

  private:
    // One slot per connection. A connection thread marks its own slot done
    // on exit (retireClient); the accept loop sweeps done slots, so the
    // list holds only live threads and shutdown joins a bounded set.
    // Retire never erases/joins (shutdown may be joining concurrently);
    // erase/swap only happens with no other thread touching the list.
    struct ClientSlot {
        std::jthread thread;
        bool done{false};
    };
    std::atomic<bool> running_{false};
    std::jthread acceptThread_;
    std::vector<ClientSlot> clients_;
    std::mutex clientsMtx_;
    std::mutex cvMtx_;
    std::condition_variable cv_;
    std::string socketPath_;
    std::stop_source stopSource_;
    // Listen handle / listen fd, shared between the accept loop (take/replace)
    // and shutdown/listen (take/close). Atomic take-over (exchange) gives
    // single ownership on every handoff -- no double-close on the shutdown
    // race. -1 = invalid on both platforms.
    std::atomic<std::intptr_t> pipeHandle_{-1};
};

} // namespace caudio::service
