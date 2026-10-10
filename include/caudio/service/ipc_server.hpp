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
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
using LPCVOID = const void*;
using LPVOID = void*;
using LPDWORD = DWORD*;
namespace caudio::service {
inline constexpr DWORD kGenericRead = 0x80000000UL;
inline constexpr DWORD kGenericWrite = 0x40000000UL;
inline constexpr DWORD kOpenExisting = 3UL;
inline constexpr DWORD kPipeAccessDuplex = 0x00000003UL;
inline constexpr DWORD kPipeTypeByte = 0x00000000UL;
inline constexpr DWORD kPipeWait = 0x00000000UL;
inline constexpr DWORD kPipeUnlimited = 255UL;
inline const HANDLE kInvalidHandle = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1));
inline constexpr DWORD kSddlRevision1 = 1UL;
} // namespace caudio::service
extern "C" {
__declspec(dllimport) HANDLE __stdcall CreateFileW(LPCWSTR, DWORD, DWORD, LPVOID, DWORD, DWORD,
                                                   HANDLE);
__declspec(dllimport) HANDLE __stdcall CreateNamedPipeW(LPCWSTR, DWORD, DWORD, DWORD, DWORD, DWORD,
                                                        DWORD, LPVOID);
__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE);
__declspec(dllimport) BOOL __stdcall ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPVOID);
__declspec(dllimport) BOOL __stdcall WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, LPVOID);
__declspec(dllimport) DWORD __stdcall GetLastError();
__declspec(dllimport) BOOL __stdcall ConnectNamedPipe(HANDLE, LPVOID);
__declspec(dllimport) BOOL __stdcall DisconnectNamedPipe(HANDLE);
__declspec(dllimport) BOOL __stdcall FlushFileBuffers(HANDLE);
__declspec(dllimport) BOOL __stdcall SetNamedPipeHandleState(HANDLE, LPDWORD, LPDWORD, LPDWORD);
__declspec(dllimport) BOOL __stdcall WaitNamedPipeW(LPCWSTR, DWORD);
__declspec(dllimport) BOOL __stdcall ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR,
                                                                                           DWORD,
                                                                                           LPVOID*,
                                                                                           LPDWORD);
__declspec(dllimport) LPVOID __stdcall LocalFree(LPVOID);
}
#else
#include <windows.h>
#endif
#endif

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
#ifdef _WIN32
    // Listen handle, shared between the accept loop (take/replace) and
    // shutdown/listen (take/close). Atomic take-over (exchange) gives single
    // ownership on every handoff -- no double-close on the shutdown race.
    std::atomic<HANDLE> pipeHandle_{nullptr};
#else
    std::atomic<int> listenFd_{-1};
#endif
};

} // namespace caudio::service
