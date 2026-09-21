#include "cli/service/ipc_server.hpp"

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
#endif

#include "caudio/utils/utils.hpp"
#include "cli/cli.hpp"
#include "cli/service/ipc_channel.hpp"

namespace caudio::service {

caudio::utils::Expected<void> IpcServer::listen(const std::filesystem::path& dbPath,
                                                std::string_view socketPathOverride) {
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
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string sockPath = socketPath_;
    if (sockPath.size() >= sizeof(addr.sun_path))
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io, "socket path too long")};
    std::memcpy(addr.sun_path, sockPath.c_str(), sockPath.size() + 1);
    listenFd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd_ < 0)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io,
                                     "socket() failed: " + std::string(std::strerror(errno)))};
    ::unlink(sockPath.c_str());
    if (::bind(listenFd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return std::unexpected{caudio::utils::makeError(
            caudio::utils::StatusCode::Io, "bind() failed: " + std::string(std::strerror(errno)))};
    }
    if (::listen(listenFd_, 8) < 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io,
                                     "listen() failed: " + std::string(std::strerror(errno)))};
    }
    return {};
#endif
}

caudio::utils::Expected<std::unique_ptr<IpcChannel>> IpcServer::accept() {
#ifdef _WIN32
    if (!pipeHandle_ || pipeHandle_ == kInvalidHandle)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::State, "pipe not initialized")};
    std::wstring w;
    w.reserve(socketPath_.size());
    for (char c : socketPath_)
        w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    HANDLE h = ::CreateNamedPipeW(w.c_str(), kPipeAccessDuplex, kPipeTypeByte | kPipeWait,
                                  kPipeUnlimited, 65536, 65536, 0, nullptr);
    if (h == kInvalidHandle)
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io,
                                                        "CreateNamedPipeW failed for accept")};
    if (!::ConnectNamedPipe(h, nullptr)) {
        DWORD err = ::GetLastError();
        if (err != 535) // ERROR_PIPE_CONNECTED
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Io, "ConnectNamedPipe failed: " + std::to_string(err))};
    }
    return std::unique_ptr<IpcChannel>{std::make_unique<WinPipeChannel>(h)};
#else
    if (listenFd_ < 0)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::State, "socket not initialized")};
    struct sockaddr_un addr{};
    socklen_t addrlen = sizeof(addr);
    int fd = ::accept(listenFd_, reinterpret_cast<struct sockaddr*>(&addr), &addrlen);
    if (fd < 0)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io,
                                     "accept() failed: " + std::string(std::strerror(errno)))};
    return std::unique_ptr<IpcChannel>{std::make_unique<UnixChannel>(fd)};
#endif
}

void IpcServer::run(
    std::stop_token st,
    std::function<caudio::cli::ReplyExpected(const caudio::cli::Command&)> dispatch) {
    running_.store(true);
    while (!st.stop_requested()) {
        auto chanRes = accept();
        if (!chanRes)
            continue;
        auto chan = std::move(*chanRes);
        auto recvRes = chan->recv();
        if (!recvRes)
            continue;
        // Convert vector<byte> payload to string for JSON deserialize
        std::string reqStr;
        reqStr.reserve(recvRes->size());
        for (auto b : *recvRes)
            reqStr.push_back(static_cast<char>(static_cast<unsigned char>(b)));
        auto reqExp = caudio::cli::deserializeRequest(reqStr);
        caudio::cli::IpcReply reply;
        if (!reqExp) {
            reply.id = 0;
            reply.result = std::unexpected{reqExp.error()};
        } else {
            reply.id = reqExp->id;
            auto resExp = dispatch(reqExp->cmd);
            reply.result = std::move(resExp);
        }
        std::string repJson = caudio::cli::serializeReply(reply);
        auto framed = caudio::cli::frame(repJson);
        (void)chan->send(framed);
    }
    running_.store(false);
}

void IpcServer::shutdown() {
    running_.store(false);
#ifdef _WIN32
    if (pipeHandle_ && pipeHandle_ != kInvalidHandle) {
        ::CloseHandle(pipeHandle_);
        pipeHandle_ = nullptr;
    }
#else
    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
    if (!socketPath_.empty()) {
        std::error_code ec;
        std::filesystem::remove(socketPath_, ec);
    }
#endif
}

IpcServer::~IpcServer() {
    shutdown();
}

#ifndef _WIN32
UnixChannel::UnixChannel(int fd) noexcept : fd_(fd) {}

UnixChannel::~UnixChannel() {
    close();
}

caudio::utils::Expected<void> UnixChannel::send(std::span<const std::byte> data) {
    if (fd_ < 0)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::State, "channel closed")};
    std::size_t total = data.size();
    std::size_t sent = 0;
    while (sent < total) {
        ssize_t n = ::write(fd_, reinterpret_cast<const char*>(data.data()) + sent, total - sent);
        if (n <= 0) {
            if (errno == EINTR)
                continue;
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io,
                                         "write failed: " + std::string(std::strerror(errno)))};
        }
        sent += static_cast<std::size_t>(n);
    }
    return {};
}

caudio::utils::Expected<std::vector<std::byte>> UnixChannel::recv() {
    if (fd_ < 0)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::State, "channel closed")};
    std::vector<std::byte> buf(4096);
    ssize_t n = ::read(fd_, buf.data(), buf.size());
    if (n <= 0) {
        if (n == 0)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io, "connection closed")};
        if (errno == EINTR)
            return std::vector<std::byte>{};
        return std::unexpected{caudio::utils::makeError(
            caudio::utils::StatusCode::Io, "read failed: " + std::string(std::strerror(errno)))};
    }
    buf.resize(static_cast<std::size_t>(n));
    return buf;
}

void UnixChannel::close() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}
#endif

#ifdef _WIN32
WinPipeChannel::WinPipeChannel(HANDLE h) noexcept : handle_(h) {}

WinPipeChannel::~WinPipeChannel() {
    close();
}

caudio::utils::Expected<void> WinPipeChannel::send(std::span<const std::byte> data) {
    if (!handle_ || handle_ == kInvalidHandle)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::State, "channel closed")};
    std::size_t total = data.size();
    std::size_t sent = 0;
    while (sent < total) {
        DWORD written = 0;
        BOOL ok = ::WriteFile(handle_, reinterpret_cast<const void*>(data.data() + sent),
                              static_cast<DWORD>(std::min<std::size_t>(total - sent, 0xFFFFFFFF)),
                              &written, nullptr);
        if (!ok) {
            DWORD err = ::GetLastError();
            if (err == 232) // ERROR_NO_DATA
                continue;
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Io, "WriteFile failed: " + std::to_string(err))};
        }
        sent += static_cast<std::size_t>(written);
    }
    ::FlushFileBuffers(handle_);
    return {};
}

caudio::utils::Expected<std::vector<std::byte>> WinPipeChannel::recv() {
    if (!handle_ || handle_ == kInvalidHandle)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::State, "channel closed")};
    std::vector<std::byte> buf(4096);
    DWORD read = 0;
    BOOL ok = ::ReadFile(handle_, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
    if (!ok) {
        DWORD err = ::GetLastError();
        if (err == 109 || err == 232) // ERROR_BROKEN_PIPE, ERROR_NO_DATA
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io, "connection closed")};
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io,
                                                        "ReadFile failed: " + std::to_string(err))};
    }
    if (read == 0)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io, "connection closed")};
    buf.resize(static_cast<std::size_t>(read));
    return buf;
}

void WinPipeChannel::close() noexcept {
    if (handle_ && handle_ != kInvalidHandle) {
        ::CloseHandle(handle_);
        handle_ = kInvalidHandle;
    }
}
#endif

} // namespace caudio::service
