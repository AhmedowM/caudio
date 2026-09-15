module;
#ifndef _WIN32
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <cstring>

module caudio.service:ipc_channel_unix;

import caudio.utils;
import :ipc_channel;

namespace caudio::service {

#ifndef _WIN32

namespace detail {

inline caudio::utils::Expected<void> sendAll(int fd, std::span<const std::byte> data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        ::ssize_t n = ::send(fd, reinterpret_cast<const char*>(data.data()) + sent,
                             data.size() - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, std::strerror(errno))};
        }
        if (n == 0) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, "send: peer closed")};
        }
        sent += static_cast<std::size_t>(n);
    }
    return {};
}

inline caudio::utils::Expected<void> recvExact(int fd, std::span<std::byte> out) {
    std::size_t got = 0;
    while (got < out.size()) {
        ::ssize_t n = ::recv(fd, reinterpret_cast<char*>(out.data()) + got, out.size() - got, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, std::strerror(errno))};
        }
        if (n == 0) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, "recv: peer closed")};
        }
        got += static_cast<std::size_t>(n);
    }
    return {};
}

} // namespace detail

class UnixSocketChannel final : public IpcChannel {
public:
    explicit UnixSocketChannel(int fd) noexcept : fd_(fd) {}
    ~UnixSocketChannel() override { close(); }

    UnixSocketChannel(const UnixSocketChannel&) = delete;
    UnixSocketChannel& operator=(const UnixSocketChannel&) = delete;
    UnixSocketChannel(UnixSocketChannel&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    UnixSocketChannel& operator=(UnixSocketChannel&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    caudio::utils::Expected<void> send(std::span<const std::byte> data) override {
        if (fd_ < 0) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::State, "channel closed")};
        }
        return detail::sendAll(fd_, data);
    }

    caudio::utils::Expected<std::vector<std::byte>> recv() override {
        if (fd_ < 0) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::State, "channel closed")};
        }
        std::array<std::byte, 4> hdr{};
        if (auto r = detail::recvExact(fd_, std::span<std::byte>(hdr)); !r) {
            return std::unexpected{r.error()};
        }
        std::uint32_t len = (static_cast<std::uint32_t>(std::to_underlying(hdr[0])) << 24) |
                            (static_cast<std::uint32_t>(std::to_underlying(hdr[1])) << 16) |
                            (static_cast<std::uint32_t>(std::to_underlying(hdr[2])) << 8) |
                            static_cast<std::uint32_t>(std::to_underlying(hdr[3]));
        if (len > (16 * 1024 * 1024)) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "frame too large")};
        }
        std::vector<std::byte> payload(len);
        if (len > 0) {
            if (auto r = detail::recvExact(fd_, std::span<std::byte>(payload)); !r) {
                return std::unexpected{r.error()};
            }
        }
        return payload;
    }

    void close() noexcept override {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int native() const noexcept { return fd_; }

    static caudio::utils::Expected<std::unique_ptr<UnixSocketChannel>> connectTo(
        const std::string& path) {
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, std::strerror(errno))};
        }
        struct FdDeleter {
            void operator()(int* p) const noexcept {
                if (p) {
                    ::close(*p);
                    delete p;
                }
            }
        };
        std::unique_ptr<int, FdDeleter> guard(new int(fd));
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (path.size() >= sizeof(addr.sun_path)) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "socket path too long")};
        }
        std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, std::strerror(errno))};
        }
        guard.release();
        auto ch = std::make_unique<UnixSocketChannel>(fd);
        return ch;
    }

    static caudio::utils::Expected<int> listenOn(const std::string& path) {
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, std::strerror(errno))};
        }
        ::unlink(path.c_str());
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (path.size() >= sizeof(addr.sun_path)) {
            ::close(fd);
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "socket path too long")};
        }
        std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            int e = errno;
            ::close(fd);
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, std::strerror(e))};
        }
        if (::listen(fd, 16) != 0) {
            int e = errno;
            ::close(fd);
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, std::strerror(e))};
        }
        return fd;
    }

private:
    int fd_{-1};
};

caudio::utils::Expected<std::unique_ptr<IpcChannel>> makeUnixChannel(int fd) {
    auto ptr = std::make_unique<UnixSocketChannel>(fd);
    std::unique_ptr<IpcChannel> base = std::move(ptr);
    return base;
}

#else // _WIN32 stub

class UnixSocketChannel final : public IpcChannel {
public:
    UnixSocketChannel() = default;
    caudio::utils::Expected<void> send(std::span<const std::byte>) override {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Unsupported, "Unix sockets not supported on Windows")};
    }
    caudio::utils::Expected<std::vector<std::byte>> recv() override {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Unsupported, "Unix sockets not supported on Windows")};
    }
    void close() noexcept override {}
};

caudio::utils::Expected<std::unique_ptr<IpcChannel>> makeUnixChannel(int) {
    return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Unsupported, "Unix sockets not supported on Windows")};
}

#endif

} // namespace caudio::service




