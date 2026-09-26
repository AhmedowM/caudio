/**
 * @file ipc_client.hpp
 * @brief IPC client for connecting to caudio service via Unix socket or Windows named pipe.
 * @ingroup caudio_client
 */
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#else
#if !defined(_WINDOWS_) && !defined(_WINDEF_) && !defined(_MINWINDEF_)
using HANDLE = void*;
using DWORD = unsigned long;
using BOOL = int;
using LPCWSTR = const wchar_t*;
using LPVOID = void*;
using LPDWORD = DWORD*;
inline constexpr DWORD kGenericRead = 0x80000000UL;
inline constexpr DWORD kGenericWrite = 0x40000000UL;
inline constexpr DWORD kOpenExisting = 3UL;
inline constexpr DWORD kPipeReadmodeByte = 0x00000000UL;
inline const HANDLE kInvalidHandle = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1));
extern "C" {
__declspec(dllimport) HANDLE __stdcall CreateFileW(LPCWSTR, DWORD, DWORD, LPVOID, DWORD, DWORD,
                                                   HANDLE);
__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE);
__declspec(dllimport) BOOL __stdcall ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPVOID);
__declspec(dllimport) BOOL __stdcall WriteFile(HANDLE, const void*, DWORD, LPDWORD, LPVOID);
__declspec(dllimport) DWORD __stdcall GetLastError();
__declspec(dllimport) BOOL __stdcall SetNamedPipeHandleState(HANDLE, LPDWORD, LPDWORD, LPDWORD);
}
#else
#include <windows.h>
#endif
#endif

#include <caudio/config.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/protocol.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/service/ipc_channel.hpp>
#include <caudio/utils.hpp>

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
     * If socketPathOverride is empty, uses cli::socketPathFor(dbPath) to derive
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
    IpcClient() = default;

    /**
     * @brief Send raw framed data.
     * @param data Byte span to send (already framed with length prefix).
     * @return void on success, Error on send failure or not connected.
     */
    caudio::utils::Expected<void> rawSend(std::span<const std::byte> data);

    /**
     * @brief Receive a framed response.
     * Reads 4-byte BE length header, then reads exact payload length.
     * Enforces 16MB max frame size.
     * @return Payload bytes (without length header) on success, Error on failure.
     */
    caudio::utils::Expected<std::vector<std::byte>> rawRecv();

    /** @brief Connected socket/named pipe path. */
    std::string socketPath_;
    /** @brief True if using Windows named pipe, false for Unix socket. */
    bool isWinPipe_{false};
#ifdef _WIN32
    /** @brief Windows named pipe handle. */
    HANDLE pipeHandle_{nullptr};
#else
    /** @brief Unix domain socket file descriptor. */
    int fd_{-1};
#endif
    /** @brief Atomic request ID counter for framing. */
    std::atomic<uint32_t> nextId_{0};
};

} // namespace caudio::client
