module;
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
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
inline constexpr DWORD kPipeReadmodeByte = 0x00000000UL;
inline constexpr DWORD kErrorMoreData = 234UL;
inline constexpr int kFalse = 0;
inline const HANDLE kInvalidHandle = reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1));
extern "C" {
__declspec(dllimport) HANDLE __stdcall CreateFileW(LPCWSTR, DWORD, DWORD, LPVOID, DWORD, DWORD, HANDLE);
__declspec(dllimport) HANDLE __stdcall CreateNamedPipeW(LPCWSTR, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPVOID);
__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE);
__declspec(dllimport) BOOL __stdcall ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPVOID);
__declspec(dllimport) BOOL __stdcall WriteFile(HANDLE, const void*, DWORD, LPDWORD, LPVOID);
__declspec(dllimport) DWORD __stdcall GetLastError();
__declspec(dllimport) BOOL __stdcall SetNamedPipeHandleState(HANDLE, LPDWORD, LPDWORD, LPDWORD);
__declspec(dllimport) BOOL __stdcall ConnectNamedPipe(HANDLE, LPVOID);
__declspec(dllimport) BOOL __stdcall DisconnectNamedPipe(HANDLE);
__declspec(dllimport) BOOL __stdcall FlushFileBuffers(HANDLE);
}
#endif

module caudio.service:ipc_channel_win;

import caudio.utils;
import :ipc_channel;

namespace caudio::service {

#ifdef _WIN32

namespace detail {

inline void closeHandleDeleter(void* h) noexcept {
    if (h && h != kInvalidHandle) {
        ::CloseHandle(h);
    }
}

using HandlePtr = std::unique_ptr<void, decltype(&closeHandleDeleter)>;

inline caudio::utils::Expected<void> writeAll(HANDLE h, std::span<const std::byte> data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        DWORD written = 0;
        BOOL ok = ::WriteFile(h, reinterpret_cast<const char*>(data.data()) + sent,
                              static_cast<DWORD>(data.size() - sent), &written, nullptr);
        if (ok == kFalse) {
            DWORD err = ::GetLastError();
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io,
                                                             "WriteFile failed: " + std::to_string(err))};
        }
        if (written == 0) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "WriteFile: zero bytes")};
        }
        sent += written;
    }
    return {};
}

inline caudio::utils::Expected<void> readExact(HANDLE h, std::span<std::byte> out) {
    std::size_t got = 0;
    while (got < out.size()) {
        DWORD r = 0;
        BOOL ok = ::ReadFile(h, reinterpret_cast<char*>(out.data()) + got,
                             static_cast<DWORD>(out.size() - got), &r, nullptr);
        if (ok == kFalse) {
            DWORD err = ::GetLastError();
            if (err == kErrorMoreData) {
                got += r;
                continue;
            }
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io,
                                                             "ReadFile failed: " + std::to_string(err))};
        }
        if (r == 0) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "ReadFile: peer closed")};
        }
        got += r;
    }
    return {};
}

} // namespace detail

class NamedPipeChannel final : public IpcChannel {
public:
    explicit NamedPipeChannel(HANDLE h) noexcept : handle_(h) {}
    ~NamedPipeChannel() override { close(); }

    NamedPipeChannel(const NamedPipeChannel&) = delete;
    NamedPipeChannel& operator=(const NamedPipeChannel&) = delete;
    NamedPipeChannel(NamedPipeChannel&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    NamedPipeChannel& operator=(NamedPipeChannel&& other) noexcept {
        if (this != &other) {
            close();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    caudio::utils::Expected<void> send(std::span<const std::byte> data) override {
        if (!handle_ || handle_ == kInvalidHandle) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::State, "pipe closed")};
        }
        return detail::writeAll(handle_, data);
    }

    caudio::utils::Expected<std::vector<std::byte>> recv() override {
        if (!handle_ || handle_ == kInvalidHandle) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::State, "pipe closed")};
        }
        std::array<std::byte, 4> hdr{};
        if (auto r = detail::readExact(handle_, std::span<std::byte>(hdr)); !r) {
            return std::unexpected{r.error()};
        }
        std::uint32_t len = (static_cast<std::uint32_t>(std::to_underlying(hdr[0])) << 24) |
                            (static_cast<std::uint32_t>(std::to_underlying(hdr[1])) << 16) |
                            (static_cast<std::uint32_t>(std::to_underlying(hdr[2])) << 8) |
                            static_cast<std::uint32_t>(std::to_underlying(hdr[3]));
        if (len > (16 * 1024 * 1024)) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "frame too large")};
        }
        std::vector<std::byte> payload(len);
        if (len > 0) {
            if (auto r = detail::readExact(handle_, std::span<std::byte>(payload)); !r) {
                return std::unexpected{r.error()};
            }
        }
        return payload;
    }

    void close() noexcept override {
        if (handle_ && handle_ != kInvalidHandle) {
            ::CloseHandle(handle_);
            handle_ = nullptr;
        }
    }

    HANDLE native() const noexcept { return handle_; }

    static caudio::utils::Expected<std::unique_ptr<NamedPipeChannel>> connectTo(const std::string& pipeName) {
        std::wstring w;
        w.reserve(pipeName.size());
        for (char c : pipeName) {
            w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        }
        HANDLE h = ::CreateFileW(w.c_str(), kGenericRead | kGenericWrite, 0, nullptr, kOpenExisting,
                                 0, nullptr);
        if (h == kInvalidHandle) {
            DWORD err = ::GetLastError();
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io,
                                                             "CreateFileW failed: " + std::to_string(err))};
        }
        DWORD mode = kPipeReadmodeByte;
        ::SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
        auto ch = std::make_unique<NamedPipeChannel>(h);
        return ch;
    }

    static caudio::utils::Expected<HANDLE> listenOn(const std::string& pipeName) {
        std::wstring w;
        w.reserve(pipeName.size());
        for (char c : pipeName) {
            w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        }
        HANDLE h = ::CreateNamedPipeW(w.c_str(), kPipeAccessDuplex, kPipeTypeByte | kPipeWait, kPipeUnlimited,
                                      65536, 65536, 0, nullptr);
        if (h == kInvalidHandle) {
            DWORD err = ::GetLastError();
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io,
                                                             "CreateNamedPipeW failed: " + std::to_string(err))};
        }
        return h;
    }

private:
    HANDLE handle_{nullptr};
};

caudio::utils::Expected<std::unique_ptr<IpcChannel>> makeNamedPipeChannel(HANDLE h) {
    auto ptr = std::make_unique<NamedPipeChannel>(h);
    std::unique_ptr<IpcChannel> base = std::move(ptr);
    return base;
}

caudio::utils::Expected<std::unique_ptr<IpcChannel>> makeNamedPipeClient(const std::string& path) {
    auto r = NamedPipeChannel::connectTo(path);
    if (!r) return std::unexpected{r.error()};
    std::unique_ptr<IpcChannel> base = std::move(*r);
    return base;
}

#else // ! _WIN32 stub

class NamedPipeChannel final : public IpcChannel {
public:
    caudio::utils::Expected<void> send(std::span<const std::byte>) override {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Unsupported, "Named pipes not supported on this platform")};
    }
    caudio::utils::Expected<std::vector<std::byte>> recv() override {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Unsupported, "Named pipes not supported on this platform")};
    }
    void close() noexcept override {}
};

caudio::utils::Expected<std::unique_ptr<IpcChannel>> makeNamedPipeChannel(void*) {
    return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Unsupported, "Named pipes not supported")};
}

caudio::utils::Expected<std::unique_ptr<IpcChannel>> makeNamedPipeClient(const std::string&) {
    return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Unsupported, "Named pipes not supported")};
}

#endif

} // namespace caudio::service
