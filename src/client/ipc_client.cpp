#include <caudio/client/ipc_client.hpp>

#include <caudio/utils.hpp>
#include <caudio/config.hpp>
#include <caudio/ipc/protocol.hpp>
#include <cstring>

namespace caudio::client {

caudio::utils::Expected<IpcClient> IpcClient::connect(const std::filesystem::path& dbPath,
                                                      std::string_view socketPathOverride) {
    caudio::utils::Expected<std::string> sp;
    if (!socketPathOverride.empty()) {
        sp = std::string(socketPathOverride.data(), socketPathOverride.size());
    } else {
        sp = caudio::cli::socketPathFor(dbPath);
    }
    if (!sp)
        return std::unexpected{sp.error()};
    std::string path = *sp;

#ifdef _WIN32
    std::wstring w;
    w.reserve(path.size());
    for (char c : path)
        w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    HANDLE h = ::CreateFileW(w.c_str(), kGenericRead | kGenericWrite, 0, nullptr, kOpenExisting, 0,
                             nullptr);
    if (h == kInvalidHandle) {
        DWORD err = ::GetLastError();
        return std::unexpected{caudio::utils::makeError(
            caudio::utils::StatusCode::Io, "CreateFileW connect failed: " + std::to_string(err))};
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
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io, "socket failed")};
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg,
                                                        "socket path too long")};
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io, "connect failed")};
    }
    IpcClient c;
    c.fd_ = fd;
    c.socketPath_ = std::move(path);
    c.isWinPipe_ = false;
    return c;
#endif
}

IpcClient::~IpcClient() {
    close();
}

IpcClient::IpcClient(IpcClient&& other) noexcept
    : socketPath_(std::move(other.socketPath_)), isWinPipe_(other.isWinPipe_)
#ifdef _WIN32
      ,
      pipeHandle_(other.pipeHandle_)
#else
      ,
      fd_(other.fd_)
#endif
{
#ifdef _WIN32
    other.pipeHandle_ = nullptr;
#else
    other.fd_ = -1;
#endif
    nextId_.store(other.nextId_.load());
}

IpcClient& IpcClient::operator=(IpcClient&& other) noexcept {
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

caudio::utils::Expected<caudio::cli::Result> IpcClient::send(const caudio::cli::Command& cmd) {
    uint32_t id = nextId_.fetch_add(1) + 1;
    caudio::cli::IpcRequest req{id, cmd};
    std::string json = caudio::cli::serializeRequest(req);
    auto framed = caudio::cli::frame(json);

    if (auto e = rawSend(framed); !e)
        return std::unexpected{e.error()};

    auto raw = rawRecv();
    if (!raw)
        return std::unexpected{raw.error()};

    // rawRecv already stripped the 4-byte BE header; construct string directly Ã¢â‚¬â€ do not call
    // deframe
    std::string replyStr;
    replyStr.reserve(raw->size());
    for (auto b : *raw)
        replyStr.push_back(static_cast<char>(static_cast<unsigned char>(b)));

    auto repExp = caudio::cli::deserializeReply(replyStr);
    if (!repExp)
        return std::unexpected{repExp.error()};
    if (!repExp->result)
        return std::unexpected{repExp->result.error()};
    return *(repExp->result);
}

void IpcClient::close() noexcept {
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

caudio::utils::Expected<void> IpcClient::rawSend(std::span<const std::byte> data) {
#ifdef _WIN32
    if (!pipeHandle_ || pipeHandle_ == kInvalidHandle) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::State, "not connected")};
    }
    std::size_t sent = 0;
    while (sent < data.size()) {
        DWORD w = 0;
        BOOL ok = ::WriteFile(pipeHandle_, reinterpret_cast<const char*>(data.data()) + sent,
                              static_cast<DWORD>(data.size() - sent), &w, nullptr);
        if (ok == 0) {
            DWORD err = ::GetLastError();
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Io, "WriteFile failed: " + std::to_string(err))};
        }
        sent += w;
    }
    return {};
#else
    if (fd_ < 0) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::State, "not connected")};
    }
    std::size_t sent = 0;
    while (sent < data.size()) {
        ::ssize_t n =
            ::send(fd_, reinterpret_cast<const char*>(data.data()) + sent, data.size() - sent, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io, "send failed")};
        }
        sent += static_cast<std::size_t>(n);
    }
    return {};
#endif
}

caudio::utils::Expected<std::vector<std::byte>> IpcClient::rawRecv() {
#ifdef _WIN32
    if (!pipeHandle_ || pipeHandle_ == kInvalidHandle) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::State, "not connected")};
    }
    std::array<std::byte, 4> hdr{};
    std::size_t got = 0;
    while (got < 4) {
        DWORD r = 0;
        BOOL ok = ::ReadFile(pipeHandle_, reinterpret_cast<char*>(hdr.data()) + got,
                             4 - static_cast<DWORD>(got), &r, nullptr);
        if (ok == 0) {
            DWORD err = ::GetLastError();
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Io, "ReadFile hdr failed: " + std::to_string(err))};
        }
        if (r == 0)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io, "pipe closed")};
        got += r;
    }
    std::uint32_t len = (static_cast<std::uint32_t>(std::to_underlying(hdr[0])) << 24) |
                        (static_cast<std::uint32_t>(std::to_underlying(hdr[1])) << 16) |
                        (static_cast<std::uint32_t>(std::to_underlying(hdr[2])) << 8) |
                        static_cast<std::uint32_t>(std::to_underlying(hdr[3]));
    if (len > 16 * 1024 * 1024) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "frame too large")};
    }
    std::vector<std::byte> payload(len);
    got = 0;
    while (got < len) {
        DWORD r = 0;
        BOOL ok = ::ReadFile(pipeHandle_, reinterpret_cast<char*>(payload.data()) + got,
                             static_cast<DWORD>(len - got), &r, nullptr);
        if (ok == 0) {
            DWORD err = ::GetLastError();
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Io, "ReadFile payload failed: " + std::to_string(err))};
        }
        if (r == 0)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io, "pipe closed")};
        got += r;
    }
    return payload;
#else
    if (fd_ < 0) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::State, "not connected")};
    }
    std::array<std::byte, 4> hdr{};
    std::size_t got = 0;
    while (got < 4) {
        ::ssize_t n = ::recv(fd_, reinterpret_cast<char*>(hdr.data()) + got, 4 - got, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io, "recv hdr failed")};
        }
        if (n == 0)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io, "peer closed")};
        got += static_cast<std::size_t>(n);
    }
    std::uint32_t len = (static_cast<std::uint32_t>(std::to_underlying(hdr[0])) << 24) |
                        (static_cast<std::uint32_t>(std::to_underlying(hdr[1])) << 16) |
                        (static_cast<std::uint32_t>(std::to_underlying(hdr[2])) << 8) |
                        static_cast<std::uint32_t>(std::to_underlying(hdr[3]));
    if (len > 16 * 1024 * 1024) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "frame too large")};
    }
    std::vector<std::byte> payload(len);
    got = 0;
    while (got < len) {
        ::ssize_t n = ::recv(fd_, reinterpret_cast<char*>(payload.data()) + got, len - got, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io, "recv payload failed")};
        }
        if (n == 0)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io, "peer closed")};
        got += static_cast<std::size_t>(n);
    }
    return payload;
#endif
}

} // namespace caudio::client
