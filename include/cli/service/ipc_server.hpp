#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
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
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#else
using HANDLE = void*;
using DWORD = unsigned long;
using BOOL = int;
using LPCWSTR = const wchar_t*;
using LPVOID = void*;
using LPDWORD = DWORD*;
inline constexpr DWORD kGenericRead = 0x80000000UL;
inline constexpr DWORD kGenericWrite = 0x40000000UL;
inline constexpr DWORD kOpenExisting = 3UL;
inline constexpr DWORD kPipeAccessDuplex = 0x00000003UL;
inline constexpr DWORD kPipeTypeByte = 0x00000000UL;
inline constexpr DWORD kPipeWait = 0x00000000UL;
inline constexpr DWORD kPipeUnlimited = 255UL;
inline const HANDLE kInvalidHandle = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1));
extern "C" {
__declspec(dllimport) HANDLE __stdcall CreateFileW(LPCWSTR, DWORD, DWORD, LPVOID, DWORD, DWORD,
                                                   HANDLE);
__declspec(dllimport) HANDLE __stdcall CreateNamedPipeW(LPCWSTR, DWORD, DWORD, DWORD, DWORD, DWORD,
                                                        DWORD, LPVOID);
__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE);
__declspec(dllimport) BOOL __stdcall ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPVOID);
__declspec(dllimport) DWORD __stdcall GetLastError();
__declspec(dllimport) BOOL __stdcall ConnectNamedPipe(HANDLE, LPVOID);
__declspec(dllimport) BOOL __stdcall DisconnectNamedPipe(HANDLE);
__declspec(dllimport) BOOL __stdcall FlushFileBuffers(HANDLE);
__declspec(dllimport) BOOL __stdcall SetNamedPipeHandleState(HANDLE, LPDWORD, LPDWORD, LPDWORD);
__declspec(dllimport) BOOL __stdcall WaitNamedPipeW(LPCWSTR, DWORD);
}
#endif

#include "caudio/utils/utils.hpp"
#include "cli/cli.hpp"
#include "cli/service/ipc_channel.hpp"

namespace caudio::service {

class IpcServer {
  public:
    IpcServer() = default;
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;
    IpcServer(IpcServer&&) = delete;
    IpcServer& operator=(IpcServer&&) = delete;

// listen binds the pipe/socket. If socketPathOverride is non-empty it is honored
    // (Config::socketPath), otherwise the canonical caudio::cli::socketPathFor(dbPath) is used.
    // Preserves public API via default arg.
    caudio::utils::Expected<void> listen(const std::filesystem::path& dbPath,
                                         std::string_view socketPathOverride = {});

    // accept waits for a new client connection and returns an IpcChannel.
    // Returns Error if not initialized or on I/O failure.
    caudio::utils::Expected<std::unique_ptr<IpcChannel>> accept();

    // run() stop semantics: accept loop exits when ANY of (external st, internal stopSource_,
    // !running_) is signaled. running_ is the primary guard (exchange true on entry, false on
    // shutdown); stopSource_ allows shutdown() to wake the loop without needing the external token;
    // st is the caller's Service::run token. Documented union.
    void run(std::stop_token st,
             std::function<caudio::cli::ReplyExpected(const caudio::cli::Command&)>
             dispatch);

    void shutdown();

  private:
    std::atomic<bool> running_{false};
    std::jthread acceptThread_;
    std::vector<std::jthread> clients_;
    std::mutex clientsMtx_;
    std::mutex cvMtx_;
    std::condition_variable cv_;
    std::string socketPath_;
    std::stop_source stopSource_;
#ifdef _WIN32
    HANDLE pipeHandle_{nullptr};
#else
    int listenFd_{-1};
#endif
};

} // namespace caudio::service

#ifndef _WIN32
// UnixChannel implementation for POSIX
class UnixChannel final : public IpcChannel {
  public:
    explicit UnixChannel(int fd) noexcept;
    ~UnixChannel() override;

    caudio::utils::Expected<void> send(std::span<const std::byte> data) override;
    caudio::utils::Expected<std::vector<std::byte>> recv() override;
    void close() noexcept override;

  private:
    int fd_{-1};
};
#else
// WinPipeChannel implementation for Windows named pipes
class WinPipeChannel final : public IpcChannel {
  public:
    explicit WinPipeChannel(HANDLE h) noexcept;
    ~WinPipeChannel() override;

    caudio::utils::Expected<void> send(std::span<const std::byte> data) override;
    caudio::utils::Expected<std::vector<std::byte>> recv() override;
    void close() noexcept override;

  private:
    HANDLE handle_{nullptr};
};
#endif

} // namespace caudio::service