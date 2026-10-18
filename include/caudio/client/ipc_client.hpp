#pragma once

/**
 * @file ipc_client.hpp
 * @brief IPC client for connecting to caudio service via Unix socket or Windows named pipe.
 * @ingroup caudio_client
 */

#include <caudio/fwd.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <filesystem>
#include <memory>
#include <string_view>

namespace caudio::client {

/**
 * @brief IPC client for communicating with the caudio service.
 *
 * Provides synchronous request/response over Unix domain sockets (POSIX) or
 * named pipes (Windows). Handles framing (4-byte big-endian length prefix),
 * JSON serialization, and connection lifecycle.
 *
 * Thread safety: Not thread-safe. Use one IpcClient per thread or synchronize externally.
 */
class IpcClient {
  public:
    /**
     * @brief Connect to the service using canonical socket path derived from dbPath.
     * @param dbPath Database path used to derive default socket/pid/lock paths.
     * @param socketPathOverride Optional explicit socket path (e.g., from --socket or config).
     * @return IpcClient on success, Error on connection failure.
     *
     * If socketPathOverride is empty, uses config::socketPathFor(dbPath) to derive
     * the canonical socket path (honors XDG/LOCALAPPDATA + hash of dbPath).
     */
    static caudio::utils::Expected<IpcClient> connect(const std::filesystem::path& dbPath,
                                                      std::string_view socketPathOverride = {});

    /**
     * @brief Destructor: closes the connection.
     */
    ~IpcClient();

    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    /**
     * @brief Move constructor.
     * Transfers ownership of the socket/pipe handle.
     */
    IpcClient(IpcClient&& other) noexcept;

    /**
     * @brief Move assignment operator.
     * Closes current connection, takes ownership of other's handle.
     */
    IpcClient& operator=(IpcClient&& other) noexcept;

    /**
     * @brief Send a command and receive the response (synchronous RPC).
     * @param cmd Command to send.
     * @return Result variant on success, Error on failure (connection, serialization, or service
     * error).
     *
     * Serializes command to JSON, frames with 4-byte BE length prefix, sends,
     * receives framed response, deserializes, and returns Result.
     * Not thread-safe; serialize calls externally if needed.
     */
    caudio::utils::Expected<caudio::ipc::Result> send(const caudio::ipc::Command& cmd);

    /**
     * @brief Close the connection (idempotent).
     * Closes socket or pipe handle, resets internal state.
     */
    void close() noexcept;

  private:
    IpcClient();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace caudio::client
