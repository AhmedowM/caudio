#include "cli/service/service_impl.hpp"

#include "cli/shared/command.hpp"
#include "cli/shared/result.hpp"
#include "cli/shared/protocol.hpp"
#include "cli/config.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "caudio/utils/utils.hpp"
#include "caudio/engine/engine.hpp"
#include "caudio/db/database.hpp"
#include "caudio/json/json.hpp"
#include "cli/service/ipc_channel.hpp"
#include "cli/service/ipc_server.hpp"
#include "cli/service/shm_status.hpp"
#include "cli/service/service_detail.hpp"

namespace caudio::service {

Service::ExpectedService Service::create(const ServiceConfig& cfg) {
    // Resolve database path
    std::filesystem::path dbPath = cfg.dbPath;
    if (dbPath.is_relative()) {
        dbPath = std::filesystem::absolute(dbPath);
    }

    // Single-instance enforcement: lock file
    auto lockPath = detail::lockPathForSocket(dbPath, "");
    int lockFd = -1;
    if (!detail::tryAcquireLock(lockPath, lockFd)) {
        return std::unexpected{caudio::utils::makeError(
            caudio::utils::StatusCode::AlreadyExists, "another instance is already running")};
    }

    // PID file handling
    auto pidPath = detail::pidPathForSocket(dbPath, "");
    if (auto existingPid = detail::readPidFile(pidPath); existingPid.has_value()) {
        if (detail::checkPidAlive(*existingPid)) {
            detail::releaseLock(lockFd);
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::AlreadyExists, "service already running (pid " +
                                                              std::to_string(*existingPid) + ")")};
        }
    }

    // Open database
    auto dbRes = caudio::db::Database::open(dbPath);
    if (!dbRes) {
        detail::releaseLock(lockFd);
        return std::unexpected{dbRes.error()};
    }
    auto db = std::make_shared<caudio::db::Database>(std::move(*dbRes));

    // Create engine
    auto eng = std::make_unique<caudio::engine::Engine>(*db);

    // Create logger
    auto logger = std::make_unique<caudio::utils::Logger>(nullptr, static_cast<caudio::utils::LogLevel>(cfg.logLevel));

    // Create IPC server
    auto srv = std::make_unique<IpcServer>();

    // Determine socket path
    std::string socketPath;
    if (!cfg.socketPath.empty()) {
        socketPath = cfg.socketPath;
    } else {
        auto sp = caudio::cli::socketPathFor(dbPath);
        if (!sp) {
            detail::releaseLock(lockFd);
            return std::unexpected{sp.error()};
        }
        socketPath = *sp;
    }

    // Create shared memory status block
    std::string shmName;
    {
        std::string dbStr = dbPath.generic_string();
        std::size_t hash = std::hash<std::string>{}(dbStr);
        shmName = std::to_string(hash);
    }
    auto shmHandle = std::make_unique<ShmStatusHandle>();
    auto shmRes = ShmStatusHandle::create(shmName, true);
    if (shmRes) {
        *shmHandle = std::move(*shmRes);
    } else {
        // SHM creation failed — log but continue (TUI polling won't work)
        logger->warn("SHM status block creation failed: {}", shmRes.error().message);
        shmHandle.reset();
    }

    // Write PID file
    {
        std::error_code ec;
        std::filesystem::create_directories(pidPath.parent_path(), ec);
        std::ofstream out(pidPath);
        if (out) {
            out << ::getpid() << "\n";
        }
    }

    auto service = std::unique_ptr<Service>(new Service(cfg, std::move(db), std::move(eng),
                                                         std::move(srv), std::move(logger),
                                                         std::move(pidPath), std::move(socketPath),
                                                         lockFd, std::move(shmHandle), std::move(shmName)));
    return service;
}

Service::Service(const ServiceConfig& cfg, std::shared_ptr<caudio::db::Database> db,
                 std::unique_ptr<caudio::engine::Engine> eng, std::unique_ptr<IpcServer> srv,
                 std::unique_ptr<caudio::utils::Logger> logger, std::filesystem::path pidPath,
                 std::string socketPath, int lockFd,
                 std::unique_ptr<caudio::service::ShmStatusHandle> shmHandle, std::string shmName)
    : config_(cfg), db_(std::move(db)), engine_(std::move(eng)), server_(std::move(srv)),
      logger_(std::move(logger)), pidPath_(std::move(pidPath)), socketPath_(std::move(socketPath)),
      lockFd_(lockFd), shmHandle_(std::move(shmHandle)), shmName_(std::move(shmName)) {}

Service::~Service() {
    shutdown();
}

caudio::utils::Expected<void> Service::run(std::stop_token st) {
    if (running_.exchange(true)) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::State, "service already running")};
    }
    shutdownRequested_.store(false);

    // Start IPC server
    auto listenRes = server_->listen(config_.dbPath, config_.socketPath);
    if (!listenRes) {
        running_.store(false);
        return std::unexpected{listenRes.error()};
    }

    server_->run(st, [this](const caudio::cli::Command& cmd) -> std::expected<caudio::cli::Result, caudio::utils::Error> {
        return this->dispatch(cmd);
    });

    // Wait for stop token
    while (!st.stop_requested() && !shutdownRequested_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    shutdown();
    running_.store(false);
    return {};
}

void Service::shutdown() {
    if (!running_.load() && !shutdownRequested_.exchange(true)) {
        return;
    }

    // Signal engine to stop
    if (engine_) {
        engine_->requestStop();
    }

    // Shutdown IPC server
    if (server_) {
        server_->shutdown();
    }

    // Remove PID file
    std::error_code ec;
    std::filesystem::remove(pidPath_, ec);

    // Release lock file
    if (lockFd_ >= 0) {
        detail::releaseLock(lockFd_);
        lockFd_ = -1;
    }

    // SHM handle cleaned up via RAII
    running_.store(false);
}

caudio::db::Database& Service::db() noexcept {
    return *db_;
}

caudio::engine::Engine& Service::engine() noexcept {
    return *engine_;
}

IpcServer& Service::server() noexcept {
    return *server_;
}

const std::string& Service::shmName() const noexcept {
    return shmName_;
}

caudio::service::ShmStatusHandle* Service::shmHandle() noexcept {
    return shmHandle_.get();
}

void Service::updateShmStatus() {
    if (!shmHandle_ || !shmHandle_->valid())
        return;

    int64_t trackId = engine_->currentTrackId();
    std::string title, artist;
    if (trackId != 0) {
        auto tr = db_->getTrack(trackId);
        if (tr) {
            title = tr->title;
            artist = tr->artist;
        }
    }

    shmHandle_->updateFromEngine(*engine_, trackId, title, artist);
    shmHandle_->setQueueSize(engine_->queueSize());
}

namespace {

struct DispatchVisitor {
    Service* self;

    template <class Cmd>
    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const Cmd&) const {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "unhandled command")};
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::Play& c) const {
        auto res = self->engine_->play(c.track_id, c.queue_id);
        if (!res) return std::unexpected{res.error()};
        self->updateShmStatus();
        return detail::buildStatus(*self->engine_, *self->db_);
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::Pause&) const {
        auto res = self->engine_->pause();
        if (!res) return std::unexpected{res.error()};
        self->updateShmStatus();
        return detail::buildStatus(*self->engine_, *self->db_);
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::Stop&) const {
        auto res = self->engine_->stop();
        if (!res) return std::unexpected{res.error()};
        self->updateShmStatus();
        return detail::buildStatus(*self->engine_, *self->db_);
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::Next&) const {
        auto res = self->engine_->next();
        if (!res) return std::unexpected{res.error()};
        self->updateShmStatus();
        return detail::buildStatus(*self->engine_, *self->db_);
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::Prev&) const {
        auto res = self->engine_->prev();
        if (!res) return std::unexpected{res.error()};
        self->updateShmStatus();
        return detail::buildStatus(*self->engine_, *self->db_);
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::Seek& c) const {
        auto res = self->engine_->seek(c.position);
        if (!res) return std::unexpected{res.error()};
        self->updateShmStatus();
        return detail::buildStatus(*self->engine_, *self->db_);
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::SetVolume& c) const {
        auto res = self->engine_->setVolume(c.volume);
        if (!res) return std::unexpected{res.error()};
        self->updateShmStatus();
        return detail::buildStatus(*self->engine_, *self->db_);
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::SetMuted& c) const {
        auto res = self->engine_->setMuted(c.muted);
        if (!res) return std::unexpected{res.error()};
        self->updateShmStatus();
        return detail::buildStatus(*self->engine_, *self->db_);
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::SetShuffle& c) const {
        auto res = self->engine_->setShuffle(c.enabled);
        if (!res) return std::unexpected{res.error()};
        self->updateShmStatus();
        return detail::buildStatus(*self->engine_, *self->db_);
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::SetRepeat& c) const {
        auto res = self->engine_->setRepeat(c.mode);
        if (!res) return std::unexpected{res.error()};
        self->updateShmStatus();
        return detail::buildStatus(*self->engine_, *self->db_);
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::AddTrack& c) const {
        auto fingerprintRes = detail::computeFingerprint(c.path);
        if (!fingerprintRes) return std::unexpected{fingerprintRes.error()};

        auto trackRes = self->db_->addTrack(c.path, fingerprintRes->data(), fingerprintRes->size());
        if (!trackRes) return std::unexpected{trackRes.error()};

        caudio::cli::Track t{};
        t.id = *trackRes;
        t.path = c.path;
        t.title = "";
        t.artist = "";
        t.duration = 0.0;
        t.fingerprint = fingerprintRes->data();

        caudio::cli::Result r{};
        r.track = std::move(t);
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::RemoveTrack& c) const {
        auto res = self->db_->removeTrack(c.track_id);
        if (!res) return std::unexpected{res.error()};
        caudio::cli::Result r{};
        r.success = true;
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::GetTrack& c) const {
        auto tr = self->db_->getTrack(c.track_id);
        if (!tr) {
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::NotFound, "track not found")};
        }
        caudio::cli::Track t{};
        t.id = tr->id;
        t.path = tr->path;
        t.title = tr->title;
        t.artist = tr->artist;
        t.duration = tr->duration;
        t.fingerprint = tr->fingerprint;

        caudio::cli::Result r{};
        r.track = std::move(t);
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::ListTracks& c) const {
        auto tracksRes = self->db_->listTracks(c.limit, c.offset);
        if (!tracksRes) return std::unexpected{tracksRes.error()};

        caudio::cli::Result r{};
        r.tracks.reserve(tracksRes->size());
        for (auto& tr : *tracksRes) {
            caudio::cli::Track t{};
            t.id = tr.id;
            t.path = tr.path;
            t.title = tr.title;
            t.artist = tr.artist;
            t.duration = tr.duration;
            t.fingerprint = tr.fingerprint;
            r.tracks.push_back(std::move(t));
        }
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::SearchTracks& c) const {
        auto tracksRes = self->db_->searchTracks(c.query, c.limit, c.offset);
        if (!tracksRes) return std::unexpected{tracksRes.error()};

        caudio::cli::Result r{};
        r.tracks.reserve(tracksRes->size());
        for (auto& tr : *tracksRes) {
            caudio::cli::Track t{};
            t.id = tr.id;
            t.path = tr.path;
            t.title = tr.title;
            t.artist = tr.artist;
            t.duration = tr.duration;
            t.fingerprint = tr.fingerprint;
            r.tracks.push_back(std::move(t));
        }
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::ScanLibrary& c) const {
        auto res = self->db_->scanLibrary(c.paths, c.recursive);
        if (!res) return std::unexpected{res.error()};
        caudio::cli::Result r{};
        r.success = true;
        r.count = *res;
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::GetStatus&) const {
        return detail::buildStatus(*self->engine_, *self->db_);
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::CreateQueue& c) const {
        auto qid = self->db_->createQueue(c.name);
        if (!qid) return std::unexpected{qid.error()};
        caudio::cli::Result r{};
        r.queue_id = *qid;
        r.success = true;
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::DeleteQueue& c) const {
        auto res = self->db_->deleteQueue(c.queue_id);
        if (!res) return std::unexpected{res.error()};
        caudio::cli::Result r{};
        r.success = true;
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::ListQueues&) const {
        auto queuesRes = self->db_->listQueues();
        if (!queuesRes) return std::unexpected{queuesRes.error()};

        caudio::cli::Result r{};
        r.queues.reserve(queuesRes->size());
        for (auto& q : *queuesRes) {
            caudio::cli::QueueInfo qi{};
            qi.id = q.id;
            qi.name = q.name;
            qi.track_count = q.track_count;
            r.queues.push_back(std::move(qi));
        }
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::SetActiveQueue& c) const {
        auto res = self->engine_->setActiveQueue(c.queue_id);
        if (!res) return std::unexpected{res.error()};
        self->updateShmStatus();
        return detail::buildStatus(*self->engine_, *self->db_);
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::QueueAdd& c) const {
        auto res = self->db_->queueAdd(c.queue_id, c.track_id, c.position);
        if (!res) return std::unexpected{res.error()};
        self->updateShmStatus();
        caudio::cli::Result r{};
        r.success = true;
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::QueueRemove& c) const {
        auto res = self->db_->queueRemove(c.queue_id, c.position);
        if (!res) return std::unexpected{res.error()};
        self->updateShmStatus();
        caudio::cli::Result r{};
        r.success = true;
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::QueueClear& c) const {
        auto res = self->db_->queueClear(c.queue_id);
        if (!res) return std::unexpected{res.error()};
        self->updateShmStatus();
        caudio::cli::Result r{};
        r.success = true;
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::QueueMove& c) const {
        auto res = self->db_->queueMove(c.queue_id, c.from_pos, c.to_pos);
        if (!res) return std::unexpected{res.error()};
        self->updateShmStatus();
        caudio::cli::Result r{};
        r.success = true;
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::QueueList& c) const {
        auto itemsRes = self->db_->queueList(c.queue_id);
        if (!itemsRes) return std::unexpected{itemsRes.error()};

        caudio::cli::Result r{};
        r.queue_items.reserve(itemsRes->size());
        for (auto& item : *itemsRes) {
            caudio::cli::QueueItem qi{};
            qi.position = item.position;
            qi.track_id = item.track_id;
            qi.queue_id = item.queue_id;
            r.queue_items.push_back(std::move(qi));
        }
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::ConfigGet& c) const {
        auto configPath = detail::resolveConfigPath(self->config_.configPath, self->config_.dbPath);
        auto valRes = detail::readConfigValueRaw(configPath, c.key);
        if (!valRes) return std::unexpected{valRes.error()};

        caudio::cli::Result r{};
        r.config_value = caudio::cli::ConfigValue{c.key, *valRes};
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::ConfigSet& c) const {
        auto configPath = detail::resolveConfigPath(self->config_.configPath, self->config_.dbPath);
        auto res = detail::writeConfigValueRaw(configPath, c.key, c.value);
        if (!res) return std::unexpected{res.error()};

        // Apply config change to engine if applicable
        if (c.key == "volume") {
            double vol = 0.0;
            auto [ptr, ec] = std::from_chars(c.value.data(), c.value.data() + c.value.size(), vol);
            if (ec == std::errc{}) {
                self->engine_->setVolume(static_cast<float>(vol));
            }
        } else if (c.key == "shuffle") {
            bool shuffle = (c.value == "true" || c.value == "1");
            self->engine_->setShuffle(shuffle);
        } else if (c.key == "repeat") {
            int mode = 0;
            auto [ptr, ec] = std::from_chars(c.value.data(), c.value.data() + c.value.size(), mode);
            if (ec == std::errc{}) {
                self->engine_->setRepeat(static_cast<caudio::engine::RepeatMode>(mode));
            }
        }

        caudio::cli::Result r{};
        r.success = true;
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::ConfigList&) const {
        auto configPath = detail::resolveConfigPath(self->config_.configPath, self->config_.dbPath);
        auto valsRes = detail::listConfigValuesRaw(configPath);
        if (!valsRes) return std::unexpected{valsRes.error()};

        caudio::cli::Result r{};
        r.config_values = *valsRes;
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::ConfigDelete& c) const {
        auto configPath = detail::resolveConfigPath(self->config_.configPath, self->config_.dbPath);
        auto res = detail::deleteConfigValueRaw(configPath, c.key);
        if (!res) return std::unexpected{res.error()};

        caudio::cli::Result r{};
        r.success = true;
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::ConfigReset&) const {
        auto configPath = detail::resolveConfigPath(self->config_.configPath, self->config_.dbPath);
        auto res = detail::resetAllConfigRaw(configPath);
        if (!res) return std::unexpected{res.error()};

        caudio::cli::Result r{};
        r.success = true;
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::GetVersion&) const {
        caudio::cli::Result r{};
        r.version = caudio::utils::kVersionFull;
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::Shutdown&) const {
        self->shutdownRequested_.store(true);
        caudio::cli::Result r{};
        r.success = true;
        return r;
    }

    std::expected<caudio::cli::Result, caudio::utils::Error> operator()(const caudio::cli::Command::Ping&) const {
        caudio::cli::Result r{};
        r.success = true;
        return r;
    }
};

} // anonymous namespace

std::expected<caudio::cli::Result, caudio::utils::Error>
Service::dispatch(const caudio::cli::Command& cmd) {
    return std::visit(DispatchVisitor{this}, cmd);
}

} // namespace caudio::service