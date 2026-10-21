#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <caudio/config.hpp>
#include <caudio/db/database.hpp>
#include <caudio/engine/types.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/protocol.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/ipc/shm_status.hpp>
#include <caudio/service/ipc_server.hpp>
#include <caudio/service/service.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/log.hpp>
#include <caudio/utils/log_sink.hpp>
#include <caudio/utils/result.hpp>
#include <caudio/version.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>

#include "config_detail.hpp"
#include "service_impl.hpp"
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
    // Daemon log first: rotating file next to the db (see logPathFor), so
    // db/engine creation below already emit. A log that cannot be opened
    // degrades to silence — logging must never block startup.
    caudio::utils::Logger::Callback logCb = [](caudio::utils::LogLevel, std::string_view) {};
    if (auto lp = caudio::config::logPathFor(cfg.dbPath)) {
        caudio::utils::FileSinkOptions sinkOpts;
        sinkOpts.teeStderr = cfg.logToStderr;
        if (auto sink = caudio::utils::FileLogSink::open(*lp, sinkOpts))
            logCb = sink->callback();
    }
    auto loggerPtr = std::make_unique<caudio::utils::Logger>(
        std::move(logCb), static_cast<caudio::utils::LogLevel>(std::clamp(
                              cfg.logLevel, std::to_underlying(caudio::utils::LogLevel::Trace),
                              std::to_underlying(caudio::utils::LogLevel::Error))));
    caudio::utils::Logger* logger = loggerPtr.get();

    // open DB
    caudio::db::DbOpts dbOpts;
    dbOpts.logger = logger;
    auto dbRes = caudio::db::Database::open(cfg.dbPath.generic_string(), dbOpts);
    if (!dbRes) {
        auto err = dbRes.error();
        std::string msg = err.message + " (" + cfg.dbPath.generic_string() + ")";
        detail::releaseLock(lockFd);
        return std::unexpected{caudio::utils::makeError(err.code, msg)};
    }
    std::shared_ptr<caudio::db::Database> dbShared(std::move(dbRes.value()));

    // create Engine
    caudio::engine::EngineConfig ecfg{};
    ecfg.logger = logger;
    ecfg.autoResumeOnPlay = cfg.autoResumeOnPlay;
    auto engRes = caudio::engine::Engine::create(ecfg);
    if (!engRes) {
        detail::releaseLock(lockFd);
        return std::unexpected{engRes.error()};
    }
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

    // Create PID file with current PID
    std::string pidStr;
    try {
        auto parent = pidPath.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }
        std::ofstream pf(pidPath);
        if (pf) {
#ifdef _WIN32
            pidStr = std::to_string(::_getpid());
#else
            pidStr = std::to_string(::getpid());
#endif
            pf << pidStr << "\n";
        }
    } catch (...) {
    }

    // Create shared memory status block for event-driven status snapshots
    // Derive shm name via canonical hex8 (consistent with socket/pid/lock)
    std::string shmName = caudio::config::detail_paths::hex8ForDb(cfg.dbPath);
    auto shmRes = caudio::ipc::createShmStatus(shmName, true);
    if (!shmRes) {
        // Non-fatal: continue without shm (clients fall back to IPC status).
        loggerPtr->warn("shm status unavailable: {} (falling back to IPC status)",
                        shmRes.error().message);
    }
    std::unique_ptr<caudio::ipc::ShmStatusHandle> shmHandle;
    if (shmRes)
        shmHandle = std::make_unique<caudio::ipc::ShmStatusHandle>(std::move(*shmRes));

    auto svc = std::unique_ptr<Service>(
        new Service(cfg, dbShared, std::move(eng), std::move(srvPtr), std::move(loggerPtr), pidPath,
                    spStr, lockFd, std::move(shmHandle), shmName));
    svc->impl_->logger_->info("daemon started version={} pid={} db={} socket={}",
                              caudio::version::version(), pidStr, cfg.dbPath.generic_string(),
                              spStr);
    return svc;
}

Service::Service(const ServiceConfig& cfg, std::shared_ptr<caudio::db::Database> db,
                 std::unique_ptr<caudio::engine::Engine> eng, std::unique_ptr<IpcServer> srv,
                 std::unique_ptr<caudio::utils::Logger> logger, std::filesystem::path pidPath,
                 std::string socketPath, std::intptr_t lockFd,
                 std::unique_ptr<caudio::ipc::ShmStatusHandle> shmHandle, std::string shmName)
    : impl_(std::make_unique<Impl>(cfg, std::move(db), std::move(eng), std::move(srv),
                                   std::move(logger), std::move(pidPath), std::move(socketPath),
                                   lockFd, std::move(shmHandle), std::move(shmName))) {}

Service::Impl::Impl(const ServiceConfig& cfg, std::shared_ptr<caudio::db::Database> db,
                    std::unique_ptr<caudio::engine::Engine> eng, std::unique_ptr<IpcServer> srv,
                    std::unique_ptr<caudio::utils::Logger> logger, std::filesystem::path pidPath,
                    std::string socketPath, std::intptr_t lockFd,
                    std::unique_ptr<caudio::ipc::ShmStatusHandle> shmHandle, std::string shmName)
    : config_(cfg), db_(std::move(db)), engine_(std::move(eng)), server_(std::move(srv)),
      logger_(std::move(logger)), pidPath_(std::move(pidPath)), socketPath_(std::move(socketPath)),
      lockFd_(lockFd), shmHandle_(std::move(shmHandle)), shmName_(std::move(shmName)) {}

caudio::utils::Expected<void> Service::run(std::stop_token st) {
    return impl_->run(st);
}

caudio::utils::Expected<void> Service::Impl::run(std::stop_token st) {
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
    if (logger_)
        logger_->info("daemon stopping");
    // Tear down here (not only in the destructor): pid/lock/socket release
    // must be sequenced before run() returns so a racing starter observes a
    // truthful state. shutdown() is idempotent; the destructor re-entry is
    // harmless.
    this->shutdown();
    return {};
}

void Service::shutdown() {
    impl_->shutdown();
}

void Service::Impl::shutdown() {
    bool was = running_.exchange(false);
    (void)was;
    shutdownRequested_.store(true, std::memory_order_release);
    if (server_)
        server_->shutdown();
    if (engine_)
        engine_->shutdown();
    // Drop ephemeral direct-play queues (their tracks stay in the library).
    if (db_) {
        if (auto qs = db_->listQueues()) {
            for (auto& q : *qs) {
                if (q.temp)
                    (void)db_->deleteQueue(q.id);
            }
        }
    }
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
    return *impl_->db_;
}
/**
 * @brief Access the audio engine instance.
 * @return Reference to the Engine.
 */
caudio::engine::Engine& Service::engine() noexcept {
    return *impl_->engine_;
}
/**
 * @brief Access the IPC server instance.
 * @return Reference to the IpcServer.
 */
IpcServer& Service::server() noexcept {
    return *impl_->server_;
}
/**
 * @brief Get the shared memory name used for status block.
 * @return Shared memory name (hash of dbPath).
 */
const std::string& Service::shmName() const noexcept {
    return impl_->shmName_;
}
/**
 * @brief Get the shared memory status handle (if available).
 * @return Pointer to ShmStatusHandle or nullptr if SHM creation failed.
 */
caudio::ipc::ShmStatusHandle* Service::shmHandle() noexcept {
    return impl_->shmHandle_.get();
}

void Service::Impl::updateShmStatus() {
    if (!shmHandle_ || !shmHandle_->valid())
        return;
    auto& eng = *engine_;
    int64_t track_id = eng.currentTrackId();
    // Single track fetch (C-2): title/artist/duration come from one lookup.
    std::string title, artist;
    double duration = 0.0;
    if (track_id != 0) {
        if (auto tr = db_->getTrack(track_id); tr) {
            title = tr->title;
            artist = tr->artist;
            duration = tr->duration;
        }
    }
    // O(1) queue size via COUNT(*) (C-2): the old listQueueItems() materialized
    // the full vector of items just for .size().
    size_t qSize = 0;
    {
        int64_t aq = eng.activeQueueId();
        qSize = db_->queueCount(aq);
    }
    caudio::ipc::ShmStatusUpdate u{};
    u.position = eng.position();
    u.volume = eng.volume();
    u.muted = (eng.volume() == 0.0f);
    u.state = static_cast<int>(eng.state());
    u.track_id = track_id;
    u.title = title;
    u.artist = artist;
    shmHandle_->publish(u);
    shmHandle_->setQueueSize(qSize);
    if (track_id != 0) {
        shmHandle_->setDuration(duration);
    }
}

std::expected<caudio::ipc::Result, caudio::utils::Error> Service::Impl::statusResult() {
    auto st = detail::buildStatus(*engine_, *db_);
    if (!st)
        return std::unexpected{st.error()};
    return caudio::ipc::Result{*st};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::Impl::dispatch(const caudio::ipc::Command& cmd) {
    // Per-command trace at debug: what the daemon was asked to do. Guarded
    // by enabled() (dump() walks the whole command) and try/catch (a log
    // line must never break dispatch on adversarial input).
    if (logger_ && logger_->enabled(caudio::utils::LogLevel::Debug)) {
        try {
            logger_->debug("dispatch {}", caudio::ipc::toJson(cmd).dump());
        } catch (...) {
        }
    }
    return std::visit(DispatchVisitor{this}, cmd);
}

} // namespace caudio::service
