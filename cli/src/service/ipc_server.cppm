module;
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
__declspec(dllimport) BOOL __stdcall WriteFile(HANDLE, const void*, DWORD, LPDWORD, LPVOID);
__declspec(dllimport) DWORD __stdcall GetLastError();
__declspec(dllimport) BOOL __stdcall ConnectNamedPipe(HANDLE, LPVOID);
__declspec(dllimport) BOOL __stdcall DisconnectNamedPipe(HANDLE);
__declspec(dllimport) BOOL __stdcall FlushFileBuffers(HANDLE);
__declspec(dllimport) BOOL __stdcall SetNamedPipeHandleState(HANDLE, LPDWORD, LPDWORD, LPDWORD);
__declspec(dllimport) BOOL __stdcall WaitNamedPipeW(LPCWSTR, DWORD);
}
#endif

export module caudio.service:ipc_server;

import caudio.utils;
import caudio.cli;
import :ipc_channel;

export namespace caudio::service {

class IpcServer {
  public:
    IpcServer() = default;
    ~IpcServer() {
        shutdown();
    }

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;
    IpcServer(IpcServer&&) = delete;
    IpcServer& operator=(IpcServer&&) = delete;

    // listen binds the pipe/socket. If socketPathOverride is non-empty it is honored
    // (Config::socketPath), otherwise the canonical caudio::cli::socketPathFor(dbPath) is used.
    // Preserves public API via default arg.
    caudio::utils::Expected<void> listen(const std::filesystem::path& dbPath,
                                         std::string_view socketPathOverride = {}) {
        caudio::utils::Expected<std::string> sp;
        if (!socketPathOverride.empty()) {
            sp = std::string(socketPathOverride.data(), socketPathOverride.size());
        } else {
            sp = caudio::cli::socketPathFor(dbPath);
        }
        if (!sp)
            return std::unexpected{sp.error()};
        socketPath_ = *sp;
        running_.store(false);

#ifdef _WIN32
        if (pipeHandle_ && pipeHandle_ != kInvalidHandle) {
            ::CloseHandle(pipeHandle_);
            pipeHandle_ = nullptr;
        }
        std::string pathStr = socketPath_;
        std::wstring w;
        w.reserve(pathStr.size());
        for (char c : pathStr)
            w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        HANDLE h = ::CreateNamedPipeW(w.c_str(), kPipeAccessDuplex, kPipeTypeByte | kPipeWait,
                                      kPipeUnlimited, 65536, 65536, 0, nullptr);
        if (h == kInvalidHandle) {
            DWORD err = ::GetLastError();
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Io, "CreateNamedPipeW failed: " + std::to_string(err))};
        }
        pipeHandle_ = h;
        return {};
#else
        if (listenFd_ >= 0) {
            ::close(listenFd_);
            listenFd_ = -1;
        }
        std::error_code ec;
        auto parent = std::filesystem::path(socketPath_).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io, "socket failed")};
        }
        std::string sockStr = socketPath_;
        ::unlink(sockStr.c_str());
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (sockStr.size() >= sizeof(addr.sun_path)) {
            ::close(fd);
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg,
                                                            "socket path too long")};
        }
        std::memcpy(addr.sun_path, sockStr.c_str(), sockStr.size() + 1);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io, "bind failed")};
        }
        if (::listen(fd, 16) != 0) {
            ::close(fd);
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io, "listen failed")};
        }
        listenFd_ = fd;
        return {};
#endif
    }

    // run() stop semantics: accept loop exits when ANY of (external st, internal stopSource_,
    // !running_) is signaled. running_ is the primary guard (exchange true on entry, false on
    // shutdown); stopSource_ allows shutdown() to wake the loop without needing the external token;
    // st is the caller's Service::run token. Documented union.
    void run(std::stop_token st,
             std::function<std::expected<caudio::cli::Result, caudio::utils::Error>(
                 const caudio::cli::Command&)>
                 dispatch) {
        if (running_.exchange(true)) {
            return;
        }
        stopSource_ = std::stop_source{};
        acceptThread_ = std::jthread([this, st, dispatch](std::stop_token jt) {
            (void)jt;
            while (!st.stop_requested() && !stopSource_.get_token().stop_requested() &&
                   running_.load()) {
#ifdef _WIN32
                if (!pipeHandle_ || pipeHandle_ == kInvalidHandle) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
                BOOL connected = ::ConnectNamedPipe(pipeHandle_, nullptr);
                if (connected == 0) {
                    DWORD err = ::GetLastError();
                    if (err == 535 /*ERROR_PIPE_CONNECTED*/) {
                        // client already connected before we called ConnectNamedPipe — treat as
                        // success
                    } else {
                        if (st.stop_requested() || stopSource_.get_token().stop_requested())
                            break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        continue;
                    }
                }
                HANDLE clientHandle = pipeHandle_;
                {
                    std::string pathStr = socketPath_;
                    std::wstring w;
                    w.reserve(pathStr.size());
                    for (char c : pathStr)
                        w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
                    HANDLE next =
                        ::CreateNamedPipeW(w.c_str(), kPipeAccessDuplex, kPipeTypeByte | kPipeWait,
                                           kPipeUnlimited, 65536, 65536, 0, nullptr);
                    if (next != kInvalidHandle) {
                        pipeHandle_ = next;
                    } else {
                        pipeHandle_ = nullptr;
                    }
                }
                {
                    std::lock_guard<std::mutex> lk(clientsMtx_);
                    clients_.emplace_back([clientHandle, dispatch](std::stop_token ct) {
                        (void)ct;
                        std::array<std::byte, 4> hdr{};
                        DWORD r = 0;
                        BOOL ok = ::ReadFile(clientHandle, hdr.data(), 4, &r, nullptr);
                        if (ok == 0 || r != 4) {
                            ::CloseHandle(clientHandle);
                            return;
                        }
                        std::uint32_t len =
                            (static_cast<std::uint32_t>(std::to_underlying(hdr[0])) << 24) |
                            (static_cast<std::uint32_t>(std::to_underlying(hdr[1])) << 16) |
                            (static_cast<std::uint32_t>(std::to_underlying(hdr[2])) << 8) |
                            static_cast<std::uint32_t>(std::to_underlying(hdr[3]));
                        std::vector<std::byte> payload(len);
                        if (len > 0) {
                            DWORD rr = 0;
                            ok = ::ReadFile(clientHandle, payload.data(), len, &rr, nullptr);
                            if (ok == 0 || rr != len) {
                                ::CloseHandle(clientHandle);
                                return;
                            }
                        }
                        // payload already unframed by the header+payload read above — construct
                        // string directly
                        std::string reqStr;
                        reqStr.reserve(payload.size());
                        for (auto b : payload)
                            reqStr.push_back(static_cast<char>(static_cast<unsigned char>(b)));
                        auto reqExp = caudio::cli::deserializeRequest(reqStr);
                        caudio::cli::IpcReply reply;
                        if (!reqExp) {
                            reply.id = 0;
                            reply.result = std::unexpected{reqExp.error()};
                        } else {
                            reply.id = reqExp->id;
                            auto resExp = dispatch(reqExp->cmd);
                            if (!resExp)
                                reply.result = std::unexpected{resExp.error()};
                            else
                                reply.result = *resExp;
                        }
                        std::string repJson = caudio::cli::serializeReply(reply);
                        auto framed = caudio::cli::frame(repJson);
                        DWORD w2 = 0;
                        ::WriteFile(clientHandle, framed.data(), static_cast<DWORD>(framed.size()),
                                    &w2, nullptr);
                        ::FlushFileBuffers(clientHandle);
                        ::DisconnectNamedPipe(clientHandle);
                        ::CloseHandle(clientHandle);
                    });
                }
#else
                if (listenFd_ < 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
                fd_set rfds;
                FD_ZERO(&rfds);
                FD_SET(listenFd_, &rfds);
                timeval tv{0, 100000};
                int sel = ::select(listenFd_ + 1, &rfds, nullptr, nullptr, &tv);
                if (sel < 0) {
                    if (errno == EINTR)
                        continue;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
                if (sel == 0)
                    continue;
                if (!FD_ISSET(listenFd_, &rfds))
                    continue;
                int cfd = ::accept(listenFd_, nullptr, nullptr);
                if (cfd < 0) {
                    if (errno == EINTR)
                        continue;
                    continue;
                }
                {
                    std::lock_guard<std::mutex> lk(clientsMtx_);
                    clients_.emplace_back([cfd, dispatch](std::stop_token ct) {
                        (void)ct;
                        std::array<std::byte, 4> hdr{};
                        auto recvExact = [cfd](std::span<std::byte> out) -> bool {
                            std::size_t got = 0;
                            while (got < out.size()) {
                                ::ssize_t n = ::recv(cfd, reinterpret_cast<char*>(out.data()) + got,
                                                     out.size() - got, 0);
                                if (n <= 0) {
                                    if (n < 0 && errno == EINTR)
                                        continue;
                                    return false;
                                }
                                got += static_cast<std::size_t>(n);
                            }
                            return true;
                        };
                        auto sendAll = [cfd](std::span<const std::byte> data) -> bool {
                            std::size_t sent = 0;
                            while (sent < data.size()) {
                                ::ssize_t n =
                                    ::send(cfd, reinterpret_cast<const char*>(data.data()) + sent,
                                           data.size() - sent, 0);
                                if (n < 0) {
                                    if (errno == EINTR)
                                        continue;
                                    return false;
                                }
                                sent += static_cast<std::size_t>(n);
                            }
                            return true;
                        };
                        if (!recvExact(std::span<std::byte>(hdr))) {
                            ::close(cfd);
                            return;
                        }
                        std::uint32_t len =
                            (static_cast<std::uint32_t>(std::to_underlying(hdr[0])) << 24) |
                            (static_cast<std::uint32_t>(std::to_underlying(hdr[1])) << 16) |
                            (static_cast<std::uint32_t>(std::to_underlying(hdr[2])) << 8) |
                            static_cast<std::uint32_t>(std::to_underlying(hdr[3]));
                        if (len > 16 * 1024 * 1024) {
                            ::close(cfd);
                            return;
                        }
                        std::vector<std::byte> payload(len);
                        if (len > 0 && !recvExact(std::span<std::byte>(payload))) {
                            ::close(cfd);
                            return;
                        }
                        std::string reqStr;
                        reqStr.reserve(payload.size());
                        for (auto b : payload)
                            reqStr.push_back(static_cast<char>(static_cast<unsigned char>(b)));
                        auto reqExp = caudio::cli::deserializeRequest(reqStr);
                        caudio::cli::IpcReply reply;
                        if (!reqExp) {
                            reply.id = 0;
                            reply.result = std::unexpected{reqExp.error()};
                        } else {
                            reply.id = reqExp->id;
                            auto resExp = dispatch(reqExp->cmd);
                            if (!resExp)
                                reply.result = std::unexpected{resExp.error()};
                            else
                                reply.result = *resExp;
                        }
                        std::string repJson = caudio::cli::serializeReply(reply);
                        auto framed = caudio::cli::frame(repJson);
                        sendAll(framed);
                        ::close(cfd);
                    });
                }
#endif
                {
                    std::unique_lock<std::mutex> lk(cvMtx_);
                    cv_.wait_for(lk, std::chrono::milliseconds(10));
                }
            }
        });
    }

    void shutdown() {
        bool was = running_.exchange(false);
        (void)was;
        stopSource_.request_stop();
        cv_.notify_all();
#ifdef _WIN32
        if (pipeHandle_ && pipeHandle_ != kInvalidHandle) {
            ::CloseHandle(pipeHandle_);
            pipeHandle_ = nullptr;
        }
        if (!socketPath_.empty()) {
            std::string pathStr = socketPath_;
            std::wstring w;
            w.reserve(pathStr.size());
            for (char c : pathStr)
                w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
            HANDLE h = ::CreateFileW(w.c_str(), kGenericRead | kGenericWrite, 0, nullptr,
                                     kOpenExisting, 0, nullptr);
            if (h != kInvalidHandle)
                ::CloseHandle(h);
        }
#else
        if (listenFd_ >= 0) {
            ::close(listenFd_);
            listenFd_ = -1;
            if (!socketPath_.empty()) {
                std::string s = socketPath_;
                ::unlink(s.c_str());
            }
        }
#endif
        if (acceptThread_.joinable()) {
            acceptThread_.request_stop();
            acceptThread_.join();
        }
        {
            std::lock_guard<std::mutex> lk(clientsMtx_);
            for (auto& t : clients_) {
                if (t.joinable())
                    t.request_stop();
            }
        }
        {
            std::lock_guard<std::mutex> lk(clientsMtx_);
            for (auto& t : clients_) {
                if (t.joinable())
                    t.join();
            }
            clients_.clear();
        }
        cv_.notify_all();
    }

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
