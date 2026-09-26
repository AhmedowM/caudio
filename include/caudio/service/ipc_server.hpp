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
}
#else
#include <windows.h>
#endif
#endif

#include <caudio/utils.hpp>
#include <caudio/ipc.hpp>
#include <caudio/service/ipc_channel.hpp>

namespace caudio::service {

class IpcServer {
  public:
    IpcServer() = default;
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;
    IpcServer(IpcServer&&) = delete;
    IpcServer& operator=(IpcServer&&) = delete;

    caudio::utils::Expected<void> listen(const std::filesystem::path& dbPath,
                                         std::string_view socketPathOverride = {});

    void run(std::stop_token st,
             std::function<std::expected<caudio::ipc::Result, caudio::utils::Error>(
                 const caudio::ipc::Command&)>
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
