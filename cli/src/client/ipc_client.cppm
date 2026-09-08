module;
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
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
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
inline constexpr DWORD kPipeReadmodeByte = 0x00000000UL;
inline const HANDLE kInvalidHandle = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1));
extern "C" {
__declspec(dllimport) HANDLE __stdcall CreateFileW(LPCWSTR, DWORD, DWORD, LPVOID, DWORD, DWORD, HANDLE);
__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE);
__declspec(dllimport) BOOL __stdcall ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPVOID);
__declspec(dllimport) BOOL __stdcall WriteFile(HANDLE, const void*, DWORD, LPDWORD, LPVOID);
__declspec(dllimport) DWORD __stdcall GetLastError();
__declspec(dllimport) BOOL __stdcall SetNamedPipeHandleState(HANDLE, LPDWORD, LPDWORD, LPDWORD);
}
#endif

export module caudio.client:ipc_client;

import caudio.utils;
import caudio.cli;
import caudio.service;

export namespace caudio::client {

class IpcClient {
public:
    static caudio::utils::Expected<IpcClient> connect(const std::filesystem::path& dbPath) {
        auto sp = caudio::service::socketPathFor(dbPath);
        if (!sp) return std::unexpected{sp.error()};
        std::string path = *sp;

#ifdef _WIN32
        std::wstring w;
        w.reserve(path.size());
        for (char c : path) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        HANDLE h = ::CreateFileW(w.c_str(), kGenericRead | kGenericWrite, 0, nullptr, kOpenExisting, 0, nullptr);
        if (h == kInvalidHandle) {
            DWORD err = ::GetLastError();
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io,
                                                             "CreateFileW connect failed: " + std::to_string(err))};
        }
        DWORD mode = kPipeReadmodeByte;
        ::SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
        IpcClient c;
        c.pipeHandle_ = h;
        c.socketPath_ = std::move(path);
        c.isWinPipe_ = true;
        return c;
#else
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "socket failed")};
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (path.size() >= sizeof(addr.sun_path)) {
            ::close(fd);
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "socket path too long")};
        }
        std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "connect failed")};
        }
        IpcClient c;
        c.fd_ = fd;
        c.socketPath_ = std::move(path);
        c.isWinPipe_ = false;
        return c;
#endif
    }

    ~IpcClient() { close(); }

    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;
    IpcClient(IpcClient&& other) noexcept
        : socketPath_(std::move(other.socketPath_)),
          isWinPipe_(other.isWinPipe_)
#ifdef _WIN32
          , pipeHandle_(other.pipeHandle_)
#else
          , fd_(other.fd_)
#endif
    {
#ifdef _WIN32
        other.pipeHandle_ = nullptr;
#else
        other.fd_ = -1;
#endif
        nextId_.store(other.nextId_.load());
    }
    IpcClient& operator=(IpcClient&& other) noexcept {
        if (this != &other) {
            close();
            socketPath_ = std::move(other.socketPath_);
            isWinPipe_ = other.isWinPipe_;
#ifdef _WIN32
            pipeHandle_ = other.pipeHandle_;
            other.pipeHandle_ = nullptr;
#else
            fd_ = other.fd_;
            other.fd_ = -1;
#endif
            nextId_.store(other.nextId_.load());
        }
        return *this;
    }

    caudio::utils::Expected<caudio::cli::Result> send(const caudio::cli::Command& cmd) {
        uint32_t id = nextId_.fetch_add(1) + 1;
        caudio::cli::IpcRequest req{id, cmd};
        std::string json = caudio::cli::serializeRequest(req);
        auto framed = caudio::cli::frame(json);

        if (auto e = rawSend(framed); !e) return std::unexpected{e.error()};

        auto raw = rawRecv();
        if (!raw) return std::unexpected{raw.error()};

        std::string replyStr;
        replyStr.reserve(raw->size());
        for (auto b : *raw) replyStr.push_back(static_cast<char>(b));

        auto def = caudio::cli::deframe(std::span<const std::byte>(raw->data(), raw->size()));
        if (def) {
            replyStr = *def;
        }

        auto repExp = caudio::cli::deserializeReply(replyStr);
        if (!repExp) return std::unexpected{repExp.error()};
        if (!repExp->result) return std::unexpected{repExp->result.error()};
        return *(repExp->result);
    }

    void close() noexcept {
#ifdef _WIN32
        if (pipeHandle_ && pipeHandle_ != kInvalidHandle) {
            ::CloseHandle(pipeHandle_);
            pipeHandle_ = nullptr;
        }
#else
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }

private:
    IpcClient() = default;

    caudio::utils::Expected<void> rawSend(std::span<const std::byte> data) {
#ifdef _WIN32
        if (!pipeHandle_ || pipeHandle_ == kInvalidHandle) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::State, "not connected")};
        }
        std::size_t sent = 0;
        while (sent < data.size()) {
            DWORD w = 0;
            BOOL ok = ::WriteFile(pipeHandle_, reinterpret_cast<const char*>(data.data()) + sent,
                                  static_cast<DWORD>(data.size() - sent), &w, nullptr);
            if (ok == 0) {
                DWORD err = ::GetLastError();
                return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "WriteFile failed: " + std::to_string(err))};
            }
            sent += w;
        }
        return {};
#else
        if (fd_ < 0) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::State, "not connected")};
        }
        std::size_t sent = 0;
        while (sent < data.size()) {
            ::ssize_t n = ::send(fd_, reinterpret_cast<const char*>(data.data()) + sent, data.size() - sent, 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "send failed")};
            }
            sent += static_cast<std::size_t>(n);
        }
        return {};
#endif
    }

    caudio::utils::Expected<std::vector<std::byte>> rawRecv() {
#ifdef _WIN32
        if (!pipeHandle_ || pipeHandle_ == kInvalidHandle) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::State, "not connected")};
        }
        std::array<std::byte, 4> hdr{};
        std::size_t got = 0;
        while (got < 4) {
            DWORD r = 0;
            BOOL ok = ::ReadFile(pipeHandle_, reinterpret_cast<char*>(hdr.data()) + got, 4 - static_cast<DWORD>(got), &r, nullptr);
            if (ok == 0) {
                DWORD err = ::GetLastError();
                return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "ReadFile hdr failed: " + std::to_string(err))};
            }
            if (r == 0) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "pipe closed")};
            got += r;
        }
        std::uint32_t len = (static_cast<std::uint32_t>(std::to_underlying(hdr[0])) << 24) |
                            (static_cast<std::uint32_t>(std::to_underlying(hdr[1])) << 16) |
                            (static_cast<std::uint32_t>(std::to_underlying(hdr[2])) << 8) |
                            static_cast<std::uint32_t>(std::to_underlying(hdr[3]));
        if (len > 16 * 1024 * 1024) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "frame too large")};
        }
        std::vector<std::byte> payload(len);
        got = 0;
        while (got < len) {
            DWORD r = 0;
            BOOL ok = ::ReadFile(pipeHandle_, reinterpret_cast<char*>(payload.data()) + got, static_cast<DWORD>(len - got), &r, nullptr);
            if (ok == 0) {
                DWORD err = ::GetLastError();
                return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "ReadFile payload failed: " + std::to_string(err))};
            }
            if (r == 0) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "pipe closed")};
            got += r;
        }
        return payload;
#else
        if (fd_ < 0) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::State, "not connected")};
        }
        std::array<std::byte, 4> hdr{};
        std::size_t got = 0;
        while (got < 4) {
            ::ssize_t n = ::recv(fd_, reinterpret_cast<char*>(hdr.data()) + got, 4 - got, 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "recv hdr failed")};
            }
            if (n == 0) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "peer closed")};
            got += static_cast<std::size_t>(n);
        }
        std::uint32_t len = (static_cast<std::uint32_t>(std::to_underlying(hdr[0])) << 24) |
                            (static_cast<std::uint32_t>(std::to_underlying(hdr[1])) << 16) |
                            (static_cast<std::uint32_t>(std::to_underlying(hdr[2])) << 8) |
                            static_cast<std::uint32_t>(std::to_underlying(hdr[3]));
        if (len > 16 * 1024 * 1024) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "frame too large")};
        }
        std::vector<std::byte> payload(len);
        got = 0;
        while (got < len) {
            ::ssize_t n = ::recv(fd_, reinterpret_cast<char*>(payload.data()) + got, len - got, 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "recv payload failed")};
            }
            if (n == 0) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "peer closed")};
            got += static_cast<std::size_t>(n);
        }
        return payload;
#endif
    }

    std::string socketPath_;
    bool isWinPipe_{false};
#ifdef _WIN32
    HANDLE pipeHandle_{nullptr};
#else
    int fd_{-1};
#endif
    std::atomic<uint32_t> nextId_{0};
    std::condition_variable cv_;
    std::mutex cvMtx_;
};

} // namespace caudio::client
