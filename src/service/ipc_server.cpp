#include <array>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/protocol.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/service/ipc_server.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/result.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <mutex>
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

#include <caudio/config.hpp>

namespace caudio::service {

IpcServer::~IpcServer() {
    shutdown();
}

caudio::utils::Expected<void> IpcServer::listen(const std::filesystem::path& dbPath,
                                                std::string_view socketPathOverride) {
    caudio::utils::Expected<std::string> sp;
    if (!socketPathOverride.empty()) {
        sp = std::string(socketPathOverride.data(), socketPathOverride.size());
    } else {
        sp = caudio::config::socketPathFor(dbPath);
    }
    if (!sp)
        return std::unexpected{sp.error()};
    socketPath_ = *sp;
    running_.store(false);

#ifdef _WIN32
    // Atomic take-over: whoever exchanges non-null owns the close.
    HANDLE prev = pipeHandle_.exchange(nullptr);
    if (prev && prev != kInvalidHandle) {
        ::CloseHandle(prev);
    }
    std::string pathStr = socketPath_;
    std::wstring w;
    w.reserve(pathStr.size());
    for (char c : pathStr)
        w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    HANDLE h = ::CreateNamedPipeW(w.c_str(), kPipeAccessDuplex, kPipeTypeByte | kPipeWait,
                                  kPipeUnlimited, 16384, 16384, 0, nullptr);
    if (h == kInvalidHandle) {
        DWORD err = ::GetLastError();
        return std::unexpected{caudio::utils::makeError(
            caudio::utils::StatusCode::Io, "CreateNamedPipeW failed: " + std::to_string(err))};
    }
    pipeHandle_.store(h);
    return {};
#else
    int prevFd = listenFd_.exchange(-1);
    if (prevFd >= 0) {
        ::close(prevFd);
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
    listenFd_.store(fd);
    return {};
#endif
}

void IpcServer::run(std::stop_token st,
                    std::function<std::expected<caudio::ipc::Result, caudio::utils::Error>(
                        const caudio::ipc::Command&)>
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
            HANDLE listenH = pipeHandle_.load();
            if (!listenH || listenH == kInvalidHandle) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            BOOL connected = ::ConnectNamedPipe(listenH, nullptr);
            if (connected == 0) {
                DWORD err = ::GetLastError();
                if (err == 535 /*ERROR_PIPE_CONNECTED*/) {
                } else {
                    if (st.stop_requested() || stopSource_.get_token().stop_requested())
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
            }
            // Take ownership of the connected instance. Shutdown may take it
            // concurrently -- whoever exchanges non-null owns the handle, so
            // a close here can never double-close. (Only this loop creates
            // instances, so a non-null take is always the connected one.)
            HANDLE clientHandle = pipeHandle_.exchange(nullptr);
            if (!clientHandle || clientHandle == kInvalidHandle) {
                // Lost the race with shutdown; loop back to the stop check.
                continue;
            }
            {
                std::string pathStr = socketPath_;
                std::wstring w;
                w.reserve(pathStr.size());
                for (char c : pathStr)
                    w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
                HANDLE next =
                    ::CreateNamedPipeW(w.c_str(), kPipeAccessDuplex, kPipeTypeByte | kPipeWait,
                                       kPipeUnlimited, 16384, 16384, 0, nullptr);
                if (next != kInvalidHandle) {
                    pipeHandle_.store(next);
                }
                // else: stay without a listener; the loop re-checks stop
                // flags and shutdown's post-join drain closes strays.
            }
            {
                std::lock_guard<std::mutex> lk(clientsMtx_);
                clients_.emplace_back();
                clients_.back().thread = std::jthread(
                    [this, clientHandle, dispatch](std::stop_token ct) {
                        (void)ct;
                        // Self-retire on every exit path (including
                        // exceptions): the slot is swept by the accept loop.
                        struct RetireGuard {
                            IpcServer* self;
                            ~RetireGuard() { self->retireClient(); }
                        } retire{this};
                        try {
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
                            if (len > 16 * 1024 * 1024) {
                                ::CloseHandle(clientHandle);
                                return;
                            }
                            std::vector<std::byte> payload(len);
                            if (len > 0) {
                                DWORD rr = 0;
                                ok = ::ReadFile(clientHandle, payload.data(), len, &rr, nullptr);
                                if (ok == 0 || rr != len) {
                                    ::CloseHandle(clientHandle);
                                    return;
                                }
                            }
                            std::string reqStr;
                            reqStr.reserve(payload.size());
                            for (auto b : payload)
                                reqStr.push_back(
                                    static_cast<char>(static_cast<unsigned char>(b)));
                            auto reqExp = caudio::ipc::deserializeRequest(reqStr);
                            caudio::ipc::IpcReply reply;
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
                            std::string repJson = caudio::ipc::serializeReply(reply);
                            auto framed = caudio::ipc::frame(repJson);
                            DWORD w2 = 0;
                            BOOL okW = ::WriteFile(clientHandle, framed.data(),
                                                   static_cast<DWORD>(framed.size()), &w2,
                                                   nullptr);
                            if (!okW) {
                                ::CloseHandle(clientHandle);
                                return;
                            }
                            ::FlushFileBuffers(clientHandle);
                            ::DisconnectNamedPipe(clientHandle);
                            ::CloseHandle(clientHandle);
                        } catch (...) {
                            ::CloseHandle(clientHandle);
                        }
                    });
                // Reap exited connections: the list holds live threads only.
                std::erase_if(clients_, [](const ClientSlot& s) { return s.done; });
            }
#else
            int listenSnapshot = listenFd_.load();
            if (listenSnapshot < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(listenSnapshot, &rfds);
            timeval tv{0, 100000};
            int sel = ::select(listenSnapshot + 1, &rfds, nullptr, nullptr, &tv);
            if (sel < 0) {
                if (errno == EINTR)
                    continue;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            if (sel == 0)
                continue;
            if (!FD_ISSET(listenSnapshot, &rfds))
                continue;
            int cfd = ::accept(listenSnapshot, nullptr, nullptr);
            if (cfd < 0) {
                if (errno == EINTR)
                    continue;
                continue;
            }
            {
                std::lock_guard<std::mutex> lk(clientsMtx_);
                clients_.emplace_back();
                clients_.back().thread = std::jthread(
                    [this, cfd, dispatch](std::stop_token ct) {
                        (void)ct;
                        // Self-retire on every exit path (including
                        // exceptions): the slot is swept by the accept loop.
                        struct RetireGuard {
                            IpcServer* self;
                            ~RetireGuard() { self->retireClient(); }
                        } retire{this};
                        try {
                            std::array<std::byte, 4> hdr{};
                            auto recvExact = [cfd](std::span<std::byte> out) -> bool {
                                std::size_t got = 0;
                                while (got < out.size()) {
                                    ::ssize_t n = ::recv(
                                        cfd, reinterpret_cast<char*>(out.data()) + got,
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
                                    ::ssize_t n = ::send(
                                        cfd, reinterpret_cast<const char*>(data.data()) + sent,
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
                                reqStr.push_back(
                                    static_cast<char>(static_cast<unsigned char>(b)));
                            auto reqExp = caudio::ipc::deserializeRequest(reqStr);
                            caudio::ipc::IpcReply reply;
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
                            std::string repJson = caudio::ipc::serializeReply(reply);
                            auto framed = caudio::ipc::frame(repJson);
                            if (!sendAll(framed)) {
                                ::close(cfd);
                                return;
                            }
                            ::close(cfd);
                        } catch (...) {
                            ::close(cfd);
                        }
                    });
                // Reap exited connections: the list holds live threads only.
                std::erase_if(clients_, [](const ClientSlot& s) { return s.done; });
            }
#endif
            {
                std::unique_lock<std::mutex> lk(cvMtx_);
                cv_.wait_for(lk, std::chrono::milliseconds(10));
            }
        }
    });
}

void IpcServer::retireClient() {
    // Marks the CALLING connection thread done. Never erases/joins/detaches:
    // shutdown may be iterating the list concurrently (it never holds
    // clientsMtx_ while joining, so this brief lock cannot deadlock it).
    const auto me = std::this_thread::get_id();
    std::lock_guard<std::mutex> lk(clientsMtx_);
    for (auto& s : clients_) {
        if (s.thread.get_id() == me) {
            s.done = true;
            break;
        }
    }
}

void IpcServer::shutdown() {
    bool was = running_.exchange(false);
    (void)was;
    stopSource_.request_stop();
    cv_.notify_all();
#ifdef _WIN32
    // Take-over (see listen()): if the accept loop is mid-handshake it owns
    // its handle; anything left here is ours to close.
    HANDLE cur = pipeHandle_.exchange(nullptr);
    if (cur && cur != kInvalidHandle) {
        ::CloseHandle(cur);
    }
    if (!socketPath_.empty()) {
        std::string pathStr = socketPath_;
        std::wstring w;
        w.reserve(pathStr.size());
        for (char c : pathStr)
            w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        HANDLE h = ::CreateFileW(w.c_str(), kGenericRead | kGenericWrite, 0, nullptr, kOpenExisting,
                                 0, nullptr);
        if (h != kInvalidHandle)
            ::CloseHandle(h);
    }
#else
    int listenCur = listenFd_.exchange(-1);
    if (listenCur >= 0) {
        ::close(listenCur);
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
#ifdef _WIN32
    // Post-join drain: the accept loop may have stored a fresh listen
    // instance after our pre-join take. Exchange is idempotent.
    HANDLE leftover = pipeHandle_.exchange(nullptr);
    if (leftover && leftover != kInvalidHandle) {
        ::CloseHandle(leftover);
    }
#endif
    // NOTE: the accept thread is already joined above, so no new entries
    // appear from here on. Joins run WITHOUT clientsMtx_: a finishing
    // connection only flips its done flag under that lock (retireClient),
    // and holding it across joins would deadlock exactly that path.
    // retireClient never erases/joins, so unlocked iteration is safe.
    {
        std::lock_guard<std::mutex> lk(clientsMtx_);
        for (auto& s : clients_) {
            if (s.thread.joinable())
                s.thread.request_stop();
        }
    }
    for (auto& s : clients_) {
        if (s.thread.joinable())
            s.thread.join();
    }
    {
        std::lock_guard<std::mutex> lk(clientsMtx_);
        clients_.clear();
    }
    cv_.notify_all();
}

} // namespace caudio::service
