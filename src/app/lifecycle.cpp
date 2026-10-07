/**
 * @file lifecycle.cpp
 * @brief Daemon lifecycle orchestration (start / shutdown / spawn).
 * @ingroup caudio_app
 * @details Moved verbatim out of the CLI shell (Phase 0): spawn a detached
 * daemon child, wait out a previous daemon's teardown, require a full
 * status round-trip before reporting success, and shut down cleanly.
 * Rendering still happens here; Phase 1 moves it to callers.
 */

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// Granular API headers used directly (spawnDaemon): kept after windows.h.
// clang-format off
#include <errhandlingapi.h>
#include <handleapi.h>
#include <libloaderapi.h>
#include <minwindef.h>
#include <processthreadsapi.h>
// clang-format on
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#include <caudio/app/core.hpp>
#include <caudio/client/core.hpp>
#include <caudio/client/ipc_client.hpp>
#include <caudio/config.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/service/core.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/print.hpp>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace caudio::app {

App::App(caudio::config::Config cfg) : config_(std::move(cfg)) {
}

App::~App() = default;

void App::setConfig(caudio::config::Config cfg) {
    config_ = std::move(cfg);
}

void App::setArgv0(std::string argv0) {
    argv0_ = std::move(argv0);
}

std::filesystem::path App::pidPathForConfig() const {
    // Canonical pid path -- single source via caudio.config (hash of dbPath + XDG/LOCALAPPDATA)
    auto r = caudio::config::pidPathFor(config_.dbPath);
    if (r)
        return *r;
    // Fallback when canonical derivation fails: pid file next to the database.
    auto pp = config_.dbPath.parent_path();
    if (pp.empty())
        pp = std::filesystem::current_path();
    return pp / "caudio.pid";
}

std::expected<void, std::uint32_t> App::spawnDaemon(const caudio::config::Config& cfg) {
#ifdef _WIN32
    wchar_t exeBuf[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        DWORD err = GetLastError();
        if (err == 0)
            err = 1;
        return std::unexpected{static_cast<std::uint32_t>(err)};
    }
    // Build mutable wide command line: "<exePath>" --daemon --foreground [--config "<configPath>"]
    std::wstring wCmd = L"\"";
    wCmd += exeBuf;
    wCmd += L"\" --daemon --foreground";
    if (!cfg.configPath.empty()) {
        // configPath may contain forward slashes; quoting protects both slash styles
        std::wstring cfgW = cfg.configPath.wstring();
        wCmd += L" --config \"";
        wCmd += cfgW;
        wCmd += L"\"";
    }
    // CreateProcessW requires mutable, null-terminated buffer
    std::vector<wchar_t> buf(wCmd.size() + 1, L'\0');
    for (size_t i = 0; i < wCmd.size(); ++i)
        buf[i] = wCmd[i];
    buf[wCmd.size()] = L'\0';
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // Use DETACHED_PROCESS only - CREATE_NEW_CONSOLE | DETACHED_PROCESS is invalid
    // (ERROR_INVALID_PARAMETER 87)
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS,
                             nullptr, nullptr, &si, &pi);
    if (!ok) {
        DWORD err = GetLastError();
        return std::unexpected{static_cast<std::uint32_t>(err)};
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return {};
#else
    pid_t pid = fork();
    if (pid < 0) {
        return std::unexpected{static_cast<std::uint32_t>(errno)};
    }
    if (pid > 0)
        return {};
    // child
    if (setsid() < 0)
        _exit(1);
    if (chdir("/") != 0) {
    }
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    std::string exePath;
#ifdef __linux__
    {
        char linkBuf[4096]{};
        ssize_t n = readlink("/proc/self/exe", linkBuf, sizeof(linkBuf) - 1);
        if (n > 0) {
            linkBuf[n] = '\0';
            exePath = linkBuf;
        } else {
            exePath = argv0_.empty() ? "/proc/self/exe" : argv0_;
        }
    }
#elif defined(__APPLE__)
    {
        char buf[4096]{};
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) == 0) {
            exePath = buf;
        } else {
            std::vector<char> dyn(size);
            if (_NSGetExecutablePath(dyn.data(), &size) == 0) {
                exePath = dyn.data();
            } else if (!argv0_.empty()) {
                exePath = argv0_;
            } else {
                exePath = "./caudio";
            }
        }
    }
#else
    // BSD / other POSIX: try /proc/curproc/file then fallback to argv0
    {
        char linkBuf[4096]{};
        ssize_t n = readlink("/proc/curproc/file", linkBuf, sizeof(linkBuf) - 1);
        if (n > 0) {
            linkBuf[n] = '\0';
            exePath = linkBuf;
        } else {
            n = readlink("/proc/self/exe", linkBuf, sizeof(linkBuf) - 1);
            if (n > 0) {
                linkBuf[n] = '\0';
                exePath = linkBuf;
            } else if (!argv0_.empty()) {
                exePath = argv0_;
            } else {
                exePath = "./caudio";
            }
        }
    }
#endif
    if (!cfg.configPath.empty()) {
        std::string cfgStr = cfg.configPath.generic_string();
        execl(exePath.c_str(), exePath.c_str(), "--config", cfgStr.c_str(), "--daemon",
              "--foreground", nullptr);
    } else {
        execl(exePath.c_str(), exePath.c_str(), "--daemon", "--foreground", nullptr);
    }
    _exit(1);
#endif
}

int App::startDaemon(bool foreground, bool quiet) {
    auto conn = caudio::client::IpcClient::connect(config_.dbPath, config_.socketPath);
    if (conn) {
        if (!quiet)
            caudio::println("daemon already running at {}", config_.socketPath);
        return 0;
    }
    // Death-watch: a previous daemon may be mid-teardown (pipe already dead
    // but pid file present / lock still held while threads join). Spawning
    // into that window dies on the lock, so wait until both the pipe is
    // unreachable AND the pid file is gone (2 s max) before spawning.
    {
        auto pidPath = pidPathForConfig();
        for (int i = 0; i < 20; ++i) {
            auto c = caudio::client::IpcClient::connect(config_.dbPath, config_.socketPath);
            std::error_code ec;
            bool pidExists = std::filesystem::exists(pidPath, ec);
            if (!c && !pidExists)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    // Ensure db parent dirs exist before trying to start service (foreground)
    {
        std::error_code ec2;
        auto parent = config_.dbPath.parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, ec2);
    }
    caudio::service::ServiceConfig scfg;
    scfg.dbPath = config_.dbPath;
    scfg.socketPath = config_.socketPath;
    scfg.configPath = config_.configPath;
    scfg.logLevel = config_.logLevel;
    if (foreground) {
        auto svc = caudio::service::Service::create(scfg);
        if (!svc) {
            caudio::println(std::cerr, "start failed: {} (db: {})", svc.error().message,
                            config_.dbPath.generic_string());
            return 1;
        }
        caudio::println("starting daemon foreground at {} db={}", config_.socketPath,
                        config_.dbPath.generic_string());
        std::stop_source ss;
        auto res = svc.value()->run(ss.get_token());
        if (!res) {
            caudio::println(std::cerr, "daemon error: {} (dbPath={})", res.error().message,
                            config_.dbPath.generic_string());
            return 1;
        }
        return 0;
    } else {
        auto spawnRes = spawnDaemon(config_);
        if (!spawnRes) {
            std::uint32_t err = spawnRes.error();
            std::string what;
            try {
                what = std::system_category().message(err);
            } catch (...) {
                what = "unknown error";
            }
            caudio::println(std::cerr, "start failed: couldn't start daemon ({}: {})", err, what);
            return 1;
        }
        // Readiness = full StatusReq round-trip (proves the accept loop and the
        // dispatcher are alive, not just that a pipe object exists -- a bare
        // connect can succeed against a dying daemon's draining instance).
        // Budget 5 s: a fresh spawn on a slow/Debug box can exceed the old
        // 1.5 s connect-only budget.
        std::string lastDetail;
        for (int i = 0; i < 50; ++i) {
            caudio::client::Client probe{config_.dbPath, config_.socketPath};
            if (auto ps = probe.send(caudio::ipc::Command{caudio::ipc::StatusReq{}},
                                     std::chrono::milliseconds{500})) {
                if (std::get_if<caudio::ipc::Status>(&*ps)) {
                    if (!quiet)
                        caudio::println("daemon started at {}", config_.socketPath);
                    return 0;
                }
                lastDetail = "unexpected reply";
            } else {
                lastDetail = ps.error().message;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        caudio::println(std::cerr, "daemon start failed: socket not reachable at {}",
                        config_.socketPath);
        if (!lastDetail.empty())
            caudio::println(std::cerr, "last error: {}", lastDetail);
        return 1;
    }
}

int App::shutdownDaemon() {
    caudio::client::Client client{config_.dbPath, config_.socketPath};
    auto cmd = caudio::ipc::Command{caudio::ipc::Shutdown{}};
    auto res = client.send(cmd, std::chrono::milliseconds{2000});
    if (!res) {
        // if daemon not running, report
        if (res.error().code == caudio::utils::StatusCode::State) {
            caudio::println(std::cerr, "shutdown: daemon not running");
            return 1;
        }
        // even if send failed, attempt to poll pid file
    }
    // poll pid file and socket until daemon exits
    auto pidPath = pidPathForConfig();
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::error_code ec;
        bool pidExists = std::filesystem::exists(pidPath, ec);
        auto conn = caudio::client::IpcClient::connect(config_.dbPath, config_.socketPath);
        if (!conn && !pidExists)
            break;
        if (!conn) {
            // socket gone, check pid file stale
            if (!pidExists)
                break;
        }
    }
    auto conn = caudio::client::IpcClient::connect(config_.dbPath, config_.socketPath);
    if (conn) {
        caudio::println(std::cerr, "shutdown: daemon still running at {}", config_.socketPath);
        return 1;
    }
    caudio::println("daemon stopped");
    return 0;
}

} // namespace caudio::app
