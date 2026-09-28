#include <algorithm>
#include <atomic>
#include <caudio/config.hpp>

#include "config_detail.hpp"
#include <caudio/db.hpp>
#include <caudio/engine.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/protocol.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/player.hpp>
#include <caudio/service/ipc_channel.hpp>
#include <caudio/service/ipc_server.hpp>
#include <caudio/service/service.hpp>
#include <caudio/service/shm_status.hpp>
#include <caudio/utils.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

#include <optional>
#include <caudio/utils/print.hpp>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "service_paths.hpp"
#include "service_status.hpp"

namespace caudio::service {

Service::~Service() {
    shutdown();
}

Service::ExpectedService Service::create(const ServiceConfig& cfg) {
    // Determine socket path
    std::string spStr;
    if (!cfg.socketPath.empty()) {
        spStr = cfg.socketPath;
    } else {
        spStr = detail::socketPathForDb(cfg.dbPath);
    }

    // Single-instance enforcement via flock lock file
    std::filesystem::path lockPath =
        detail::lockPathForSocket(cfg.dbPath, cfg.socketPath.empty() ? spStr : cfg.socketPath);
    std::intptr_t lockFd = -1;
    if (!detail::tryAcquireLock(lockPath, lockFd)) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::AlreadyExists,
                                                        "service already running (lock held)")};
    }

    // Check for stale PID file
    std::filesystem::path pidPath =
        detail::pidPathForSocket(cfg.dbPath, cfg.socketPath.empty() ? spStr : cfg.socketPath);
    std::error_code ec;
    if (std::filesystem::exists(pidPath, ec)) {
        auto existingPid = detail::readPidFile(pidPath);
        if (existingPid && detail::checkPidAlive(*existingPid)) {
            // Process is alive, daemon already running
            detail::releaseLock(lockFd);
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::AlreadyExists, "service already running (pid alive)")};
        }
        // Stale PID - remove it
        std::filesystem::remove(pidPath, ec);
    }

    // Check for stale socket
#ifdef _WIN32
    if (!spStr.empty() && spStr.starts_with("\\\\")) {
        if (detail::probeSocketAlive(spStr)) {
            detail::releaseLock(lockFd);
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::AlreadyExists, "service already running (pipe alive)")};
        }
    } else
#endif
        if (!spStr.empty() && !spStr.starts_with("\\\\")) {
        std::filesystem::path sockP(spStr);
        if (std::filesystem::exists(sockP, ec)) {
            if (detail::probeSocketAlive(spStr)) {
                detail::releaseLock(lockFd);
                return std::unexpected{
                    caudio::utils::makeError(caudio::utils::StatusCode::AlreadyExists,
                                             "service already running (socket alive)")};
            } else {
                // Stale socket - remove
                std::filesystem::remove(sockP, ec);
            }
        }
    }

    // ensure db parent dirs exist before open (fixes "unable to open database file")
    {
        std::error_code ec2;
        auto parent = cfg.dbPath.parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, ec2);
    }
    // open DB
    auto dbRes = caudio::db::Database::open(cfg.dbPath.generic_string());
    if (!dbRes) {
        auto err = dbRes.error();
        std::string msg = err.message + " (" + cfg.dbPath.generic_string() + ")";
        return std::unexpected{caudio::utils::makeError(err.code, msg)};
    }
    std::shared_ptr<caudio::db::Database> dbShared(std::move(dbRes.value()));

    // create Engine
    caudio::engine::EngineConfig ecfg{};
    auto engRes = caudio::engine::Engine::create(ecfg);
    if (!engRes)
        return std::unexpected{engRes.error()};
    std::unique_ptr<caudio::engine::Engine> eng = std::move(engRes.value());
    if (auto e = eng->attachDatabase(dbShared); !e) {
        detail::releaseLock(lockFd);
        return std::unexpected{e.error()};
    }

    // ipc server listen -- honor Config::socketPath if set (canonical override), else derive
    // from dbPath
    auto srvPtr = std::make_unique<IpcServer>();
    auto listenRes = srvPtr->listen(cfg.dbPath, cfg.socketPath);
    if (!listenRes) {
        detail::releaseLock(lockFd);
        return std::unexpected{listenRes.error()};
    }

    auto loggerPtr = std::make_unique<caudio::utils::Logger>(
        [](caudio::utils::LogLevel lvl, std::string_view msg) {
            (void)lvl;
            (void)msg;
        },
        static_cast<caudio::utils::LogLevel>(std::clamp(cfg.logLevel, 0, 3)));

    // Create PID file with current PID
    try {
        auto parent = pidPath.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }
        std::ofstream pf(pidPath);
        if (pf) {
#ifdef _WIN32
            pf << ::_getpid();
#else
            pf << ::getpid();
#endif
            pf << "\n";
        }
    } catch (...) {
    }

    // Create shared memory status block for TUI 10fps polling
    // Derive shm name via canonical hex8 (consistent with socket/pid/lock)
    std::string shmName = caudio::config::detail_paths::hex8ForDb(cfg.dbPath);
    auto shmRes = caudio::service::createShmStatus(shmName, true);
    if (!shmRes) {
        // Non-fatal: log but continue without shm
    }
    std::unique_ptr<caudio::service::ShmStatusHandle> shmHandle;
    if (shmRes)
        shmHandle = std::make_unique<caudio::service::ShmStatusHandle>(std::move(*shmRes));

    auto svc = std::unique_ptr<Service>(
        new Service(cfg, dbShared, std::move(eng), std::move(srvPtr), std::move(loggerPtr), pidPath,
                    spStr, lockFd, std::move(shmHandle), shmName));
    return svc;
}

Service::Service(const ServiceConfig& cfg, std::shared_ptr<caudio::db::Database> db,
                 std::unique_ptr<caudio::engine::Engine> eng, std::unique_ptr<IpcServer> srv,
                 std::unique_ptr<caudio::utils::Logger> logger, std::filesystem::path pidPath,
                 std::string socketPath, std::intptr_t lockFd,
                 std::unique_ptr<caudio::service::ShmStatusHandle> shmHandle, std::string shmName)
    : config_(cfg), db_(std::move(db)), engine_(std::move(eng)), server_(std::move(srv)),
      logger_(std::move(logger)), pidPath_(std::move(pidPath)), socketPath_(std::move(socketPath)),
      lockFd_(lockFd), shmHandle_(std::move(shmHandle)), shmName_(std::move(shmName)) {}

caudio::utils::Expected<void> Service::run(std::stop_token st) {
    if (running_.exchange(true)) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::State, "already running")};
    }
    // build dispatcher
    auto dispatcher = [this](const caudio::ipc::Command& cmd)
        -> std::expected<caudio::ipc::Result, caudio::utils::Error> { return this->dispatch(cmd); };
    if (server_)
        server_->run(st, dispatcher);
    // block until stop requested
    while (!st.stop_requested() && !shutdownRequested_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return {};
}

void Service::shutdown() {
    bool was = running_.exchange(false);
    (void)was;
    shutdownRequested_.store(true, std::memory_order_release);
    if (server_)
        server_->shutdown();
    if (engine_)
        engine_->shutdown();
    // cleanup pid file and socket
    try {
        std::error_code ec;
        if (!pidPath_.empty() && std::filesystem::exists(pidPath_, ec)) {
            std::filesystem::remove(pidPath_, ec);
        }
        if (!socketPath_.empty()) {
            std::string s = socketPath_;
            if (!s.starts_with("\\\\") && !s.empty()) {
                std::filesystem::path sp(s);
                if (std::filesystem::exists(sp, ec)) {
                    std::filesystem::remove(sp, ec);
                }
            }
        }
        // cleanup lock file
        if (lockFd_ >= 0) {
            detail::releaseLock(lockFd_);
            lockFd_ = -1;
            auto lockPath = detail::lockPathForSocket(config_.dbPath, socketPath_);
            std::filesystem::remove(lockPath, ec);
        }
    } catch (...) {
    }
    // shm handle will be cleaned up via RAII
}

caudio::db::Database& Service::db() noexcept {
    return *db_;
}
/**
 * @brief Access the audio engine instance.
 * @return Reference to the Engine.
 */
caudio::engine::Engine& Service::engine() noexcept {
    return *engine_;
}
/**
 * @brief Access the IPC server instance.
 * @return Reference to the IpcServer.
 */
IpcServer& Service::server() noexcept {
    return *server_;
}
/**
 * @brief Get the shared memory name used for status block.
 * @return Shared memory name (hash of dbPath).
 */
const std::string& Service::shmName() const noexcept {
    return shmName_;
}
/**
 * @brief Get the shared memory status handle (if available).
 * @return Pointer to ShmStatusHandle or nullptr if SHM creation failed.
 */
caudio::service::ShmStatusHandle* Service::shmHandle() noexcept {
    return shmHandle_.get();
}

void Service::updateShmStatus() {
    if (!shmHandle_ || !shmHandle_->valid())
        return;
    auto& eng = *engine_;
    int64_t track_id = eng.currentTrackId();
    std::string title, artist;
    if (track_id != 0) {
        auto tr = db_->getTrack(track_id);
        if (tr) {
            title = tr->title;
            artist = tr->artist;
        }
    }
    // Get queue size for active queue
    size_t qSize = 0;
    {
        int64_t aq = eng.activeQueueId();
        if (auto items = db_->queueList(aq); items) {
            qSize = items->size();
        }
    }
    shmHandle_->updateFromEngine(eng, track_id, title, artist);
    shmHandle_->setQueueSize(qSize);
    // duration is not directly available from engine, would need track info
    if (track_id != 0) {
        auto tr = db_->getTrack(track_id);
        if (tr) {
            shmHandle_->setDuration(tr->duration);
        }
    }
}

std::expected<caudio::ipc::Result, caudio::utils::Error> Service::statusResult() {
    auto st = detail::buildStatus(*engine_, *db_);
    if (!st)
        return std::unexpected{st.error()};
    return caudio::ipc::Result{*st};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::dispatch(const caudio::ipc::Command& cmd) {
    return std::visit(DispatchVisitor{this}, cmd);
}

} // namespace caudio::service
