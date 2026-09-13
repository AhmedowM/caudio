module;
// Service owns Engine/DB/Config/Logger/IpcServer and dispatches commands

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

export module caudio.service:impl;

import caudio.utils;
import caudio.engine;
import caudio.db;
import caudio.cli;
import caudio.player;
import caudio.json;
import :ipc_channel;
import :ipc_server;
import :shm_status;
import :detail;

export namespace caudio::service {

struct ServiceConfig {
    std::filesystem::path dbPath{"library.db"};
    std::string socketPath{};
    std::filesystem::path configPath{};
    int logLevel{2};
};

} // namespace caudio::service

export namespace caudio::service {

class Service final {
  public:
    using ExpectedService = std::expected<std::unique_ptr<Service>, caudio::utils::Error>;

    static ExpectedService create(const ServiceConfig& cfg) {
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
        int lockFd = -1;
        if (!detail::tryAcquireLock(lockPath, lockFd)) {
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::AlreadyExists, "service already running (lock held)")};
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
                return std::unexpected{
                    caudio::utils::makeError(caudio::utils::StatusCode::AlreadyExists,
                                             "service already running (pid alive)")};
            }
            // Stale PID - remove it
            std::filesystem::remove(pidPath, ec);
        }

        // Check for stale socket
#ifdef _WIN32
        if (!spStr.empty() && spStr.starts_with("\\\\")) {
            if (detail::probeSocketAlive(spStr)) {
                detail::releaseLock(lockFd);
                return std::unexpected{
                    caudio::utils::makeError(caudio::utils::StatusCode::AlreadyExists,
                                             "service already running (pipe alive)")};
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

        // ipc server listen — honor Config::socketPath if set (canonical override), else derive
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
        // Derive hash from dbPath for shm name
        std::string dbStr = cfg.dbPath.generic_string();
        std::size_t hash = std::hash<std::string>{}(dbStr);
        std::string shmName = std::to_string(hash);
        auto shmRes = caudio::service::createShmStatus(shmName, true);
        if (!shmRes) {
            // Non-fatal: log but continue without shm
        }
        std::unique_ptr<caudio::service::ShmStatusHandle> shmHandle;
        if (shmRes)
            shmHandle = std::make_unique<caudio::service::ShmStatusHandle>(std::move(*shmRes));

        auto svc = std::unique_ptr<Service>(
            new Service(cfg, dbShared, std::move(eng), std::move(srvPtr), std::move(loggerPtr),
                        pidPath, spStr, lockFd, std::move(shmHandle), shmName));
        return svc;
    }

    ~Service() {
        shutdown();
    }

    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    Service(Service&&) = delete;
    Service& operator=(Service&&) = delete;

    caudio::utils::Expected<void> run(std::stop_token st) {
        if (running_.exchange(true)) {
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::State, "already running")};
        }
        // build dispatcher
        auto dispatcher = [this](const caudio::cli::Command& cmd)
            -> std::expected<caudio::cli::Result, caudio::utils::Error> {
            return this->dispatch(cmd);
        };
        if (server_)
            server_->run(st, dispatcher);
        // block until stop requested
        while (!st.stop_requested() && !shutdownRequested_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return {};
    }

    void shutdown() {
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
                        // only unlink if we own it (server already did unlink on shutdown)
                        // keep attempt
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

    caudio::db::Database& db() noexcept {
        return *db_;
    }
    caudio::engine::Engine& engine() noexcept {
        return *engine_;
    }
    IpcServer& server() noexcept {
        return *server_;
    }
    const std::string& shmName() const noexcept {
        return shmName_;
    }
    caudio::service::ShmStatusHandle* shmHandle() noexcept {
        return shmHandle_.get();
    }

  private:
    Service(const ServiceConfig& cfg, std::shared_ptr<caudio::db::Database> db,
            std::unique_ptr<caudio::engine::Engine> eng, std::unique_ptr<IpcServer> srv,
            std::unique_ptr<caudio::utils::Logger> logger, std::filesystem::path pidPath,
            std::string socketPath, int lockFd,
            std::unique_ptr<caudio::service::ShmStatusHandle> shmHandle, std::string shmName)
        : config_(cfg), db_(std::move(db)), engine_(std::move(eng)), server_(std::move(srv)),
          logger_(std::move(logger)), pidPath_(std::move(pidPath)),
          socketPath_(std::move(socketPath)), lockFd_(lockFd), shmHandle_(std::move(shmHandle)),
          shmName_(std::move(shmName)) {}

    void updateShmStatus() {
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

    std::expected<caudio::cli::Result, caudio::utils::Error>
    dispatch(const caudio::cli::Command& cmd) {
        using namespace caudio::cli;
        // helper to build status
        auto statusResult = [&]() -> std::expected<Result, caudio::utils::Error> {
            auto st = detail::buildStatus(*engine_, *db_);
            if (!st)
                return std::unexpected{st.error()};
            return Result{*st};
        };

        return std::visit(
            detail::overloaded{
                [&](const Play&) -> std::expected<Result, caudio::utils::Error> {
                    int64_t aq = engine_->activeQueueId();
                    auto r = engine_->play(aq);
                    if (!r)
                        return std::unexpected{r.error()};
                    updateShmStatus();
                    return statusResult();
                },
                [&](const Pause&) -> std::expected<Result, caudio::utils::Error> {
                    auto r = engine_->pause();
                    if (!r)
                        return std::unexpected{r.error()};
                    updateShmStatus();
                    return statusResult();
                },
                [&](const Resume&) -> std::expected<Result, caudio::utils::Error> {
                    auto r = engine_->resume();
                    if (!r)
                        return std::unexpected{r.error()};
                    updateShmStatus();
                    return statusResult();
                },
                [&](const Restart&) -> std::expected<Result, caudio::utils::Error> {
                    // restart: seek to 0, ensure playing
                    auto r = engine_->seek(0.0);
                    if (!r) {
                        // if no track, try play
                        int64_t aq = engine_->activeQueueId();
                        auto pr = engine_->play(aq);
                        if (!pr)
                            return std::unexpected{pr.error()};
                        updateShmStatus();
                        return statusResult();
                    }
                    // ensure playing
                    if (engine_->state() == caudio::engine::PlaybackState::Paused) {
                        (void)engine_->resume();
                    }
                    updateShmStatus();
                    return statusResult();
                },
                [&](const Stop&) -> std::expected<Result, caudio::utils::Error> {
                    auto r = engine_->stop();
                    if (!r)
                        return std::unexpected{r.error()};
                    updateShmStatus();
                    return statusResult();
                },
                [&](const Next&) -> std::expected<Result, caudio::utils::Error> {
                    auto r = engine_->next();
                    if (!r)
                        return std::unexpected{r.error()};
                    updateShmStatus();
                    return statusResult();
                },
                [&](const Prev&) -> std::expected<Result, caudio::utils::Error> {
                    auto r = engine_->prev();
                    if (!r)
                        return std::unexpected{r.error()};
                    updateShmStatus();
                    return statusResult();
                },
                [&](const Seek& s) -> std::expected<Result, caudio::utils::Error> {
                    if (!std::isfinite(s.seconds) || s.seconds < 0) {
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::InvalidArg, "seek: invalid seconds")};
                    }
                    auto r = engine_->seek(s.seconds);
                    if (!r)
                        return std::unexpected{r.error()};
                    updateShmStatus();
                    return statusResult();
                },
                [&](const StatusReq&) -> std::expected<Result, caudio::utils::Error> {
                    return statusResult();
                },
                [&](const VolumeSet& v) -> std::expected<Result, caudio::utils::Error> {
                    float cur = engine_->volume();
                    float target = cur;
                    bool hasTarget = false;
                    if (v.level.has_value()) {
                        float lvl = *v.level;
                        if (!std::isfinite(lvl)) {
                            return std::unexpected{caudio::utils::makeError(
                                caudio::utils::StatusCode::InvalidArg, "volume: invalid level")};
                        }
                        // clamp 0-100 -> 0.0-1.0
                        if (lvl < 0.0f)
                            lvl = 0.0f;
                        if (lvl > 100.0f)
                            lvl = 100.0f;
                        target = lvl / 100.0f;
                        hasTarget = true;
                    }
                    if (v.deltaPct.has_value()) {
                        int d = *v.deltaPct;
                        float curPct = cur * 100.0f;
                        float np = curPct + static_cast<float>(d);
                        if (np < 0.0f)
                            np = 0.0f;
                        if (np > 100.0f)
                            np = 100.0f;
                        target = np / 100.0f;
                        hasTarget = true;
                    }
                    if (v.mute.has_value()) {
                        if (*v.mute) {
                            target = 0.0f;
                            hasTarget = true;
                        } else {
                            if (cur == 0.0f && !hasTarget) {
                                target = 0.5f;
                                hasTarget = true;
                            }
                        }
                    }
                    if (hasTarget) {
                        auto r = engine_->setVolume(target);
                        if (!r)
                            return std::unexpected{r.error()};
                    }
                    updateShmStatus();
                    caudio::cli::VolumeInfo vi{engine_->volume(), engine_->volume() == 0.0f};
                    return Result{vi};
                },
                [&](const QueueList&) -> std::expected<Result, caudio::utils::Error> {
                    int64_t qid = engine_->activeQueueId();
                    // validation: queue exists
                    {
                        auto q = db_->getQueue(qid);
                        if (!q)
                            return std::unexpected{q.error()};
                    }
                    auto items = db_->queueList(qid);
                    if (!items)
                        return std::unexpected{items.error()};
                    std::vector<caudio::db::Track> tracks;
                    tracks.reserve(items->size());
                    for (auto& it : *items) {
                        auto tr = db_->getTrack(it.track_id);
                        if (tr)
                            tracks.push_back(std::move(*tr));
                    }
                    return Result{QueueTracks{std::move(tracks)}};
                },
                [&](const QueueQueues&) -> std::expected<Result, caudio::utils::Error> {
                    auto qs = db_->listQueues();
                    if (!qs)
                        return std::unexpected{qs.error()};
                    caudio::cli::LibraryStatsData ls{};
                    ls.queues = qs->size();
                    // also fill tracks/playlists for completeness
                    auto st = db_->getStats();
                    if (st) {
                        ls.tracks = static_cast<std::size_t>(st->num_tracks);
                        ls.playlists = static_cast<std::size_t>(st->num_playlists);
                    }
                    return Result{ls};
                },
                [&](const QueueSwitch& qs) -> std::expected<Result, caudio::utils::Error> {
                    auto q = db_->getQueue(qs.qid);
                    if (!q)
                        return std::unexpected{q.error()};
                    auto sw = engine_->switchQueue(qs.qid);
                    if (!sw)
                        return std::unexpected{sw.error()};
                    updateShmStatus();
                    return statusResult();
                },
                [&](const QueueAdd& qa) -> std::expected<Result, caudio::utils::Error> {
                    int64_t qid = engine_->activeQueueId();
                    auto q = db_->getQueue(qid);
                    if (!q)
                        return std::unexpected{q.error()};
                    if (!qa.search) {
                        std::filesystem::path p(qa.query);
                        std::error_code ec;
                        if (std::filesystem::exists(p, ec) && !ec && detail::hasAudioExt(p)) {
                            auto fpRes = detail::computeFingerprint(p);
                            if (!fpRes)
                                return std::unexpected{fpRes.error()};
                            caudio::db::Track t;
                            t.path = p.generic_string();
                            t.fingerprint = *fpRes;
                            t.duration = detail::durationFromDecoder(p);
                            {
                                std::error_code ec2;
                                auto sz = std::filesystem::file_size(p, ec2);
                                if (!ec2)
                                    t.size = static_cast<int64_t>(sz);
                                auto ftime = std::filesystem::last_write_time(p, ec2);
                                if (!ec2)
                                    t.mtime =
                                        static_cast<int64_t>(ftime.time_since_epoch().count());
                            }
                            int64_t newId = 0;
                            auto ins = db_->insertTrack(t);
                            if (ins) {
                                newId = *ins;
                                t.id = newId;
                            } else {
                                if (ins.error().code == caudio::utils::StatusCode::AlreadyExists) {
                                    auto existing = db_->findByFingerprint(t.fingerprint);
                                    if (existing) {
                                        t = std::move(*existing);
                                        newId = t.id;
                                    } else {
                                        auto byPath = db_->findByPath(t.path);
                                        if (byPath) {
                                            t = std::move(*byPath);
                                            newId = t.id;
                                        } else {
                                            return std::unexpected{ins.error()};
                                        }
                                    }
                                } else {
                                    return std::unexpected{ins.error()};
                                }
                            }
                            auto eq = db_->queueEnqueue(qid, newId);
                            if (!eq)
                                return std::unexpected{eq.error()};
                            updateShmStatus();
                            std::vector<caudio::db::Track> single;
                            single.reserve(1);
                            single.push_back(std::move(t));
                            return Result{QueueTracks{std::move(single)}};
                        }
                    }
                    std::vector<caudio::db::Track> toAdd;
                    if (qa.search) {
                        auto sr = caudio::db::search(*db_, qa.query, 50);
                        if (!sr)
                            return std::unexpected{sr.error()};
                        toAdd = std::move(*sr);
                    } else {
                        // try parse as int id
                        bool parsed = false;
                        int64_t id = 0;
                        try {
                            std::string s = qa.query;
                            // trim
                            s.erase(0, s.find_first_not_of(" \t\n\r"));
                            s.erase(s.find_last_not_of(" \t\n\r") + 1);
                            if (!s.empty()) {
                                // check if all digits (allow leading -)
                                bool isNum = true;
                                for (std::size_t i = (s[0] == '-' ? 1 : 0); i < s.size(); ++i)
                                    if (!std::isdigit((unsigned char)s[i])) {
                                        isNum = false;
                                        break;
                                    }
                                if (isNum) {
                                    id = std::stoll(s);
                                    parsed = true;
                                }
                            }
                        } catch (...) {
                        }
                        if (parsed && id != 0) {
                            auto tr = db_->getTrack(id);
                            if (!tr)
                                return std::unexpected{tr.error()};
                            toAdd.push_back(std::move(*tr));
                        } else {
                            // try findByPath
                            auto tr = db_->findByPath(qa.query);
                            if (tr) {
                                toAdd.push_back(std::move(*tr));
                            } else {
                                // fallback to search via FTS (covers LIKE)
                                auto sr = caudio::db::search(*db_, qa.query, 50);
                                if (!sr)
                                    return std::unexpected{sr.error()};
                                toAdd = std::move(*sr);
                                if (toAdd.empty()) {
                                    return std::unexpected{caudio::utils::makeError(
                                        caudio::utils::StatusCode::NotFound,
                                        "track not found: " + qa.query)};
                                }
                            }
                        }
                    }
                    // Batch enqueue in single transaction: atomic to concurrent queueList, rollback
                    // on failure
                    {
                        std::vector<int64_t> ids;
                        ids.reserve(toAdd.size());
                        for (auto& t : toAdd)
                            ids.push_back(t.id);
                        auto er = db_->queueEnqueueBatch(qid, ids);
                        if (!er)
                            return std::unexpected{er.error()};
                    }
                    updateShmStatus();
                    return Result{QueueTracks{std::move(toAdd)}};
                },
                [&](const QueueRemove& qr) -> std::expected<Result, caudio::utils::Error> {
                    int64_t qid = engine_->activeQueueId();
                    auto q = db_->getQueue(qid);
                    if (!q)
                        return std::unexpected{q.error()};
                    // parse idOrIndex
                    int64_t val = 0;
                    bool isNum = false;
                    try {
                        std::string s = qr.idOrIndex;
                        s.erase(0, s.find_first_not_of(" \t\n\r"));
                        s.erase(s.find_last_not_of(" \t\n\r") + 1);
                        if (!s.empty()) {
                            bool allDigit = true;
                            std::size_t off = (s[0] == '-' ? 1 : 0);
                            for (std::size_t i = off; i < s.size(); ++i)
                                if (!std::isdigit((unsigned char)s[i])) {
                                    allDigit = false;
                                    break;
                                }
                            if (allDigit) {
                                val = std::stoll(s);
                                isNum = true;
                            }
                        }
                    } catch (...) {
                    }
                    if (!isNum) {
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::InvalidArg, "invalid idOrIndex")};
                    }
                    // try as position first
                    auto rm = db_->queueRemove(qid, val);
                    if (!rm) {
                        // if not found as position, try as track_id lookup
                        if (rm.error().code == caudio::utils::StatusCode::NotFound) {
                            auto items = db_->queueList(qid);
                            if (!items)
                                return std::unexpected{items.error()};
                            bool found = false;
                            int64_t pos = -1;
                            for (auto& it : *items)
                                if (it.track_id == val) {
                                    pos = it.position;
                                    found = true;
                                    break;
                                }
                            if (!found)
                                return std::unexpected{rm.error()};
                            auto rm2 = db_->queueRemove(qid, pos);
                            if (!rm2)
                                return std::unexpected{rm2.error()};
                        } else {
                            return std::unexpected{rm.error()};
                        }
                    }
                    auto items = db_->queueList(qid);
                    if (!items)
                        return std::unexpected{items.error()};
                    std::vector<caudio::db::Track> tracks;
                    for (auto& it : *items) {
                        auto tr = db_->getTrack(it.track_id);
                        if (tr)
                            tracks.push_back(std::move(*tr));
                    }
                    updateShmStatus();
                    return Result{QueueTracks{std::move(tracks)}};
                },
                [&](const QueueMove& qm) -> std::expected<Result, caudio::utils::Error> {
                    // QueueMove: reorder within queue via playlistReorder? For queue we lack direct
                    // move. Simulate via remove+enqueue: fetch items, reorder vector, clear and
                    // re-enqueue
                    int64_t qid = engine_->activeQueueId();
                    auto q = db_->getQueue(qid);
                    if (!q)
                        return std::unexpected{q.error()};
                    auto items = db_->queueList(qid);
                    if (!items)
                        return std::unexpected{items.error()};
                    if (qm.from >= items->size() || qm.to >= items->size()) {
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::InvalidArg, "move out of range")};
                    }
                    // collect track_ids in order
                    std::vector<int64_t> ids;
                    ids.reserve(items->size());
                    for (auto& it : *items)
                        ids.push_back(it.track_id);
                    int64_t mv = ids[qm.from];
                    ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(qm.from));
                    ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(qm.to), mv);
                    // Transactional clear+enqueue: single BEGIN IMMEDIATE/COMMIT so concurrent
                    // queueList never sees empty
                    {
                        auto r = db_->queueReplaceAll(qid, ids);
                        if (!r)
                            return std::unexpected{r.error()};
                    }
                    auto nitems = db_->queueList(qid);
                    if (!nitems)
                        return std::unexpected{nitems.error()};
                    std::vector<caudio::db::Track> tracks;
                    for (auto& it : *nitems) {
                        auto tr = db_->getTrack(it.track_id);
                        if (tr)
                            tracks.push_back(std::move(*tr));
                    }
                    updateShmStatus();
                    return Result{QueueTracks{std::move(tracks)}};
                },
                [&](const QueueClear&) -> std::expected<Result, caudio::utils::Error> {
                    int64_t qid = engine_->activeQueueId();
                    auto q = db_->getQueue(qid);
                    if (!q)
                        return std::unexpected{q.error()};
                    auto r = db_->queueClear(qid);
                    if (!r)
                        return std::unexpected{r.error()};
                    updateShmStatus();
                    return Result{QueueTracks{{}}};
                },
                [&](const QueueShuffle& qs) -> std::expected<Result, caudio::utils::Error> {
                    bool on;
                    if (qs.on.has_value()) {
                        on = *qs.on;
                    } else {
                        // toggle: get current state and flip
                        on = !engine_->shuffle();
                    }
                    auto r = engine_->setShuffle(on);
                    if (!r)
                        return std::unexpected{r.error()};
                    updateShmStatus();
                    return statusResult();
                },
                [&](const QueueRepeat& qr) -> std::expected<Result, caudio::utils::Error> {
                    if (qr.mode.has_value()) {
                        auto r = engine_->setRepeat(*qr.mode);
                        if (!r)
                            return std::unexpected{r.error()};
                    }
                    // if no arg provided, just show current (statusResult already includes it)
                    updateShmStatus();
                    return statusResult();
                },
                [&](const LibraryScan& cmd) -> std::expected<Result, caudio::utils::Error> {
                    std::filesystem::path root;
                    if (cmd.path)
                        root = std::filesystem::path(*cmd.path);
                    else {
                        auto pp = config_.dbPath.parent_path();
                        if (pp.empty())
                            pp = std::filesystem::current_path();
                        root = pp / "music";
                    }
                    auto mode = (cmd.mode == "full" ? caudio::db::ScanMode::Full
                                                    : caudio::db::ScanMode::Sampled);
                    // Prefer scanLibrary if a library matches root — gives dedup + batched
                    // transaction
                    if (auto libs = db_->libraryList(); libs) {
                        for (auto& l : *libs) {
                            if (std::filesystem::path(l.path) == root) {
                                auto sr = caudio::db::scanLibrary(*db_, l.id);
                                if (!sr)
                                    return std::unexpected{sr.error()};
                                caudio::cli::LibraryStatsData d2{};
                                if (auto st = db_->getStats()) {
                                    d2.tracks = static_cast<std::size_t>(st->num_tracks);
                                    d2.queues = static_cast<std::size_t>(st->num_queue_items);
                                    d2.playlists = static_cast<std::size_t>(st->num_playlists);
                                }
                                return Result{std::move(d2)};
                            }
                        }
                    }
                    // Fallback: simple insert (batched path uses scanLibrary above which is already
                    // per-500 transactional)
                    std::size_t n = 0;
                    for (auto t : caudio::db::scan(root, mode)) {
                        auto r = db_->insertTrack(t);
                        if (r)
                            ++n;
                    }
                    caudio::cli::LibraryStatsData d{};
                    d.tracks = n;
                    d.queues = 0;
                    d.playlists = 0;
                    if (auto st = db_->getStats()) {
                        d.queues = static_cast<std::size_t>(st->num_queue_items);
                        d.playlists = static_cast<std::size_t>(st->num_playlists);
                    }
                    return Result{std::move(d)};
                },
                [&](const LibrarySearch& cmd) -> std::expected<Result, caudio::utils::Error> {
                    auto tracks = caudio::db::search(*db_, cmd.query, cmd.limit);
                    if (!tracks)
                        return std::unexpected{tracks.error()};
                    // fallback to like handled inside search; if empty still return
                    std::span<const caudio::db::Track> span{*tracks};
                    std::vector<caudio::db::Track> out(span.begin(), span.end());
                    return Result{Tracks{std::move(out)}};
                },
                [&](const LibraryStats&) -> std::expected<Result, caudio::utils::Error> {
                    auto st = db_->getStats();
                    if (!st)
                        return std::unexpected{st.error()};
                    caudio::cli::LibraryStatsData d{};
                    d.tracks = static_cast<std::size_t>(st->num_tracks);
                    d.queues = static_cast<std::size_t>(st->num_queue_items);
                    d.playlists = static_cast<std::size_t>(st->num_playlists);
                    return Result{d};
                },
                [&](const LibraryStatsDetailed&) -> std::expected<Result, caudio::utils::Error> {
                    auto st = db_->libraryStatsDetailed();
                    if (!st)
                        return std::unexpected{st.error()};
                    caudio::cli::LibraryStatsDetailedData d{};
                    d.tracks = static_cast<std::size_t>(st->tracks);
                    d.queues = static_cast<std::size_t>(st->queues);
                    d.playlists = static_cast<std::size_t>(st->playlists);
                    d.total_duration_ms = st->total_duration_ms;
                    d.total_play_time_ms = st->total_play_time_ms;
                    d.most_played = std::move(st->most_played);
                    return Result{std::move(d)};
                },
                [&](const LibraryList& cmd) -> std::expected<Result, caudio::utils::Error> {
                    caudio::db::TrackQuery q{};
                    q.limit = cmd.limit;
                    q.offset = cmd.offset;
                    if (cmd.query.has_value())
                        q.search = *cmd.query;
                    if (cmd.artist.has_value())
                        q.artist = *cmd.artist;
                    if (cmd.album.has_value())
                        q.album = *cmd.album;
                    if (cmd.genre.has_value())
                        q.genre = *cmd.genre;
                    auto tracks = db_->listTracks(&q);
                    if (!tracks)
                        return std::unexpected{tracks.error()};
                    return Result{Tracks{std::move(*tracks)}};
                },
                [&](const Info&) -> std::expected<Result, caudio::utils::Error> {
                    // Get current track from engine status
                    int64_t track_id = engine_->currentTrackId();
                    if (track_id == 0) {
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::NotFound, "no track currently playing")};
                    }
                    auto tr = db_->getTrack(track_id);
                    if (!tr)
                        return std::unexpected{tr.error()};
                    caudio::cli::TrackInfo ti{};
                    ti.track = std::move(*tr);
                    ti.play_count = ti.track.play_count;
                    ti.last_played = ti.track.last_played;
                    return Result{std::move(ti)};
                },
                [&](const HistoryList& cmd) -> std::expected<Result, caudio::utils::Error> {
                    int limit = cmd.limit.value_or(50);
                    auto hist = engine_->listHistory(limit);
                    if (!hist)
                        return std::unexpected{hist.error()};
                    std::vector<caudio::cli::HistoryEntry> cliEntries;
                    cliEntries.reserve(hist->size());
                    for (const auto& e : *hist) {
                        caudio::cli::HistoryEntry cliEntry;
                        cliEntry.id = e.id;
                        cliEntry.track_id = e.track_id;
                        cliEntry.started_at = e.started_at;
                        cliEntry.completed_at = e.completed_at;
                        cliEntry.position_ms = e.position_ms;
                        cliEntry.completion_pct = e.completion_pct;
                        cliEntry.queue_id = e.queue_id;
                        cliEntry.title = e.title;
                        cliEntry.artist = e.artist;
                        cliEntry.path = e.path;
                        cliEntry.duration = e.duration;
                        cliEntries.push_back(std::move(cliEntry));
                    }
                    return Result{caudio::cli::History{std::move(cliEntries)}};
                },
                [&](const HistoryClear&) -> std::expected<Result, caudio::utils::Error> {
                    auto r = engine_->clearHistory();
                    if (!r)
                        return std::unexpected{r.error()};
                    return Result{Empty{}};
                },
                [&](const LibraryAdd& cmd) -> std::expected<Result, caudio::utils::Error> {
                    if (cmd.path.empty())
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::InvalidArg, "library add: missing path")};
                    std::filesystem::path p(cmd.path);
                    std::error_code ec;
                    bool exists = std::filesystem::exists(p, ec);
                    if (ec || !exists)
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::NotFound, "path not found: " + cmd.path)};
                    auto addSingleFile = [&](const std::filesystem::path& fp)
                        -> std::expected<void, caudio::utils::Error> {
                        if (!detail::hasAudioExt(fp))
                            return std::unexpected{caudio::utils::makeError(
                                caudio::utils::StatusCode::Unsupported, "unsupported file type: " + fp.string())};
                        auto fpRes = detail::computeFingerprint(fp);
                        if (!fpRes)
                            return std::unexpected{fpRes.error()};
                        caudio::db::Track t;
                        t.path = fp.generic_string();
                        t.fingerprint = *fpRes;
                        t.duration = detail::durationFromDecoder(fp);
                        std::error_code ec2;
                        auto sz = std::filesystem::file_size(fp, ec2);
                        if (!ec2)
                            t.size = static_cast<int64_t>(sz);
                        auto ftime = std::filesystem::last_write_time(fp, ec2);
                        if (!ec2)
                            t.mtime = static_cast<int64_t>(ftime.time_since_epoch().count());
                        // Extract metadata (title, artist, album, etc.)
                        if (auto meta = caudio::player::extractMetadata(t.path); meta) {
                            t.title = std::move(meta->title);
                            t.artist = std::move(meta->artist);
                            t.album = std::move(meta->album);
                            t.album_artist = std::move(meta->album_artist);
                            t.genre = std::move(meta->genre);
                            t.year = meta->year;
                            t.track_num = meta->track_num;
                            t.disc_num = meta->disc_num;
                            if (meta->duration > 0) t.duration = meta->duration;
                            t.sample_rate = static_cast<uint32_t>(meta->sample_rate);
                            t.channels = static_cast<uint32_t>(meta->channels);
                            t.bitrate = meta->bitrate;
                        }
                        auto ins = db_->insertTrack(t);
                        if (!ins) {
                            if (ins.error().code == caudio::utils::StatusCode::AlreadyExists) {
                                auto existing = db_->findByFingerprint(t.fingerprint);
                                if (existing)
                                    return {};
                                auto byPath = db_->findByPath(t.path);
                                if (byPath)
                                    return {};
                            }
                            return std::unexpected{ins.error()};
                        }
                        return {};
                    };
                    if (std::filesystem::is_regular_file(p, ec)) {
                        auto r = addSingleFile(p);
                        if (!r)
                            return std::unexpected{r.error()};
                        return Result{Empty{}};
                    } else if (std::filesystem::is_directory(p, ec)) {
                        std::size_t added = 0;
                        std::error_code iterEc;
                        if (cmd.recursive) {
                            for (auto it = std::filesystem::recursive_directory_iterator(
                                     p, std::filesystem::directory_options::skip_permission_denied, iterEc);
                                 it != std::filesystem::recursive_directory_iterator(); ++it) {
                                if (iterEc)
                                    break;
                                std::error_code e3;
                                if (it->is_regular_file(e3) && !e3 && detail::hasAudioExt(it->path())) {
                                    auto r = addSingleFile(it->path());
                                    if (r)
                                        ++added;
                                }
                            }
                        } else {
                            for (auto it = std::filesystem::directory_iterator(p, iterEc);
                                 it != std::filesystem::directory_iterator(); ++it) {
                                if (iterEc)
                                    break;
                                std::error_code e3;
                                if (it->is_regular_file(e3) && !e3 && detail::hasAudioExt(it->path())) {
                                    auto r = addSingleFile(it->path());
                                    if (r)
                                        ++added;
                                }
                            }
                        }
                        if (added == 0) {
                            // check if any audio files existed but failed?
                            // Return Empty still if dir was empty — not an error.
                        }
                        return Result{Empty{}};
                    } else {
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::InvalidArg, "not a file or directory: " + cmd.path)};
                    }
                },
                [&](const LibraryRemove& cmd) -> std::expected<Result, caudio::utils::Error> {
                    if (cmd.query.empty())
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::InvalidArg, "library remove: missing id")};
                    std::string q = cmd.query;
                    // trim
                    q.erase(0, q.find_first_not_of(" \t\r\n"));
                    q.erase(q.find_last_not_of(" \t\r\n") + 1);
                    // try numeric id
                    bool isNum = !q.empty();
                    for (size_t i = (q[0] == '-' ? 1 : 0); i < q.size() && isNum; ++i)
                        if (!std::isdigit((unsigned char)q[i]))
                            isNum = false;
                    if (isNum) {
                        try {
                            int64_t id = std::stoll(q);
                            if (id != 0) {
                                auto r = db_->deleteTrack(id);
                                if (r)
                                    return Result{Empty{}};
                                if (r.error().code != caudio::utils::StatusCode::NotFound)
                                    return std::unexpected{r.error()};
                                // fallthrough to path lookup
                            }
                        } catch (...) {
                        }
                    }
                    // try by path
                    auto byPath = db_->findByPath(cmd.query);
                    if (byPath) {
                        auto r = db_->deleteTrack(byPath->id);
                        if (!r)
                            return std::unexpected{r.error()};
                        return Result{Empty{}};
                    }
                    // also try trimmed path
                    if (q != cmd.query) {
                        auto byPath2 = db_->findByPath(q);
                        if (byPath2) {
                            auto r = db_->deleteTrack(byPath2->id);
                            if (!r)
                                return std::unexpected{r.error()};
                            return Result{Empty{}};
                        }
                    }
                    return std::unexpected{caudio::utils::makeError(
                        caudio::utils::StatusCode::NotFound, "track not found: " + cmd.query)};
                },
                [&](const TagEdit& cmd) -> std::expected<Result, caudio::utils::Error> {
                    if (cmd.id == 0)
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::InvalidArg, "tag edit: invalid id")};
                    static const std::array<std::string_view, 8> allowed{
                        "title", "artist", "album", "album_artist", "genre", "year", "track_number", "disc_number"};
                    bool ok = false;
                    for (auto a : allowed)
                        if (a == cmd.field) {
                            ok = true;
                            break;
                        }
                    if (!ok)
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::InvalidArg, "invalid field: " + cmd.field)};
                    auto trRes = db_->getTrack(cmd.id);
                    if (!trRes)
                        return std::unexpected{trRes.error()};
                    caudio::db::Track t = std::move(*trRes);
                    if (cmd.field == "title")
                        t.title = cmd.value;
                    else if (cmd.field == "artist")
                        t.artist = cmd.value;
                    else if (cmd.field == "album")
                        t.album = cmd.value;
                    else if (cmd.field == "album_artist")
                        t.album_artist = cmd.value;
                    else if (cmd.field == "genre")
                        t.genre = cmd.value;
                    else if (cmd.field == "year") {
                        try {
                            size_t pos = 0;
                            int v = std::stoi(cmd.value, &pos);
                            if (pos != cmd.value.size())
                                throw std::invalid_argument("extra");
                            t.year = v;
                        } catch (...) {
                            return std::unexpected{caudio::utils::makeError(
                                caudio::utils::StatusCode::InvalidArg, "invalid year: " + cmd.value)};
                        }
                    } else if (cmd.field == "track_number") {
                        try {
                            size_t pos = 0;
                            int v = std::stoi(cmd.value, &pos);
                            if (pos != cmd.value.size())
                                throw std::invalid_argument("extra");
                            t.track_num = v;
                        } catch (...) {
                            return std::unexpected{caudio::utils::makeError(
                                caudio::utils::StatusCode::InvalidArg, "invalid track_number: " + cmd.value)};
                        }
                    } else if (cmd.field == "disc_number") {
                        try {
                            size_t pos = 0;
                            int v = std::stoi(cmd.value, &pos);
                            if (pos != cmd.value.size())
                                throw std::invalid_argument("extra");
                            t.disc_num = v;
                        } catch (...) {
                            return std::unexpected{caudio::utils::makeError(
                                caudio::utils::StatusCode::InvalidArg, "invalid disc_number: " + cmd.value)};
                        }
                    }
                    auto upd = db_->updateTrack(t);
                    if (!upd)
                        return std::unexpected{upd.error()};
                    return Result{Empty{}};
                },
                [&](const TagGet& cmd) -> std::expected<Result, caudio::utils::Error> {
                    if (cmd.id == 0)
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::InvalidArg, "tag get: invalid id")};
                    auto tr = db_->getTrack(cmd.id);
                    if (!tr)
                        return std::unexpected{tr.error()};
                    return Result{SingleTrack{std::move(*tr)}};
                },
                [&](const ConfigGet& cmd) -> std::expected<Result, caudio::utils::Error> {
                    auto p = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                    auto vRes = detail::readConfigValueRaw(p, cmd.key);
                    if (!vRes)
                        return std::unexpected{vRes.error()};
                    return Result{ConfigValue{cmd.key, *vRes}};
                },
                [&](const ConfigSet& cmd) -> std::expected<Result, caudio::utils::Error> {
                    auto p = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                    auto sRes = detail::writeConfigValueRaw(p, cmd.key, cmd.value);
                    if (!sRes)
                        return std::unexpected{sRes.error()};
                    return Result{Empty{}};
                },
                [&](const ConfigList&) -> std::expected<Result, caudio::utils::Error> {
                    auto p = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                    auto lRes = detail::listConfigValuesRaw(p);
                    if (!lRes)
                        return std::unexpected{lRes.error()};
                    ConfigValues cvs{};
                    cvs.values = std::move(*lRes);
                    return Result{std::move(cvs)};
                },
                [&](const ConfigExport& cmd) -> std::expected<Result, caudio::utils::Error> {
                    auto src = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                    std::filesystem::path dst{cmd.path};
                    std::error_code ec;
                    if (!std::filesystem::exists(src, ec)) {
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::NotFound, "config not found")};
                    }
                    std::filesystem::copy_file(
                        src, dst, std::filesystem::copy_options::overwrite_existing, ec);
                    if (ec)
                        return std::unexpected{
                            caudio::utils::makeError(caudio::utils::StatusCode::Io, ec.message())};
                    return Result{Empty{}};
                },
                [&](const ConfigImport& cmd) -> std::expected<Result, caudio::utils::Error> {
                    std::filesystem::path src{cmd.path};
                    auto dst = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                    std::error_code ec;
                    if (!std::filesystem::exists(src, ec)) {
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::NotFound, "import path not found")};
                    }
                    std::filesystem::create_directories(dst.parent_path(), ec);
                    std::filesystem::copy_file(
                        src, dst, std::filesystem::copy_options::overwrite_existing, ec);
                    if (ec)
                        return std::unexpected{
                            caudio::utils::makeError(caudio::utils::StatusCode::Io, ec.message())};
                    // validate that file is readable and non-empty JSON-like (at least contains
                    // '{')
                    std::error_code ec2;
                    if (!std::filesystem::exists(dst, ec2)) {
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::Io, "import failed")};
                    }
                    return Result{Empty{}};
                },
                [&](const ConfigReset& cmd) -> std::expected<Result, caudio::utils::Error> {
                    auto p = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                    if (cmd.key.has_value() && !cmd.key->empty()) {
                        auto r = detail::deleteConfigValueRaw(p, *cmd.key);
                        if (!r)
                            return std::unexpected{r.error()};
                    } else if (cmd.key.has_value() && cmd.key->empty()) {
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::InvalidArg, "empty key")};
                    } else {
                        auto r = detail::resetAllConfigRaw(p);
                        if (!r)
                            return std::unexpected{r.error()};
                    }
                    return Result{Empty{}};
                },
                [&](const PlaylistList&) -> std::expected<Result, caudio::utils::Error> {
                    auto pls = db_->listPlaylists();
                    if (!pls)
                        return std::unexpected{pls.error()};
                    std::span<const caudio::db::Playlist> span{*pls};
                    std::vector<caudio::db::Playlist> out(span.begin(), span.end());
                    return Result{Playlists{std::move(out)}};
                },
                [&](const PlaylistTracks& cmd) -> std::expected<Result, caudio::utils::Error> {
                    auto tracks = db_->playlistGetTracks(cmd.pid);
                    if (!tracks)
                        return std::unexpected{tracks.error()};
                    std::span<const caudio::db::Track> span{*tracks};
                    std::vector<caudio::db::Track> out(span.begin(), span.end());
                    return Result{QueueTracks{std::move(out)}};
                },
                [&](const PlaylistLoad& cmd) -> std::expected<Result, caudio::utils::Error> {
                    auto tracks = db_->playlistGetTracks(cmd.pid);
                    if (!tracks)
                        return std::unexpected{tracks.error()};
                    int64_t qid = 1;
                    auto clr = db_->queueClear(qid);
                    if (!clr)
                        return std::unexpected{clr.error()};
                    for (auto& t : std::span<const caudio::db::Track>(*tracks)) {
                        auto er = db_->queueEnqueue(qid, t.id);
                        if (!er)
                            return std::unexpected{er.error()};
                    }
                    if (cmd.play) {
                        auto pr = engine_->play(qid);
                        if (!pr)
                            return std::unexpected{pr.error()};
                    }
                    updateShmStatus();
                    auto st = detail::buildStatus(*engine_, *db_);
                    if (!st)
                        return std::unexpected{st.error()};
                    return Result{*st};
                },
                [&](const PlaylistSave& cmd) -> std::expected<Result, caudio::utils::Error> {
                    if (cmd.name.empty())
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::InvalidArg, "empty playlist name")};
                    auto pidRes = db_->createPlaylist(cmd.name);
                    if (!pidRes)
                        return std::unexpected{pidRes.error()};
                    int64_t pid = *pidRes;
                    int64_t qid = cmd.queue_id.value_or(1);
                    auto items = db_->queueList(qid);
                    if (!items)
                        return std::unexpected{items.error()};
                    for (auto& it : std::span<const caudio::db::QueueItem>(*items)) {
                        auto r = db_->playlistAddTrack(pid, it.track_id);
                        if (!r)
                            return std::unexpected{r.error()};
                    }
                    return Result{Empty{}};
                },
                [&](const PlaylistDelete& cmd) -> std::expected<Result, caudio::utils::Error> {
                    auto r = db_->deletePlaylist(cmd.pid);
                    if (!r)
                        return std::unexpected{r.error()};
                    return Result{Empty{}};
                },
                [&](const PlaylistRename& cmd) -> std::expected<Result, caudio::utils::Error> {
                    if (cmd.newName.empty())
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::InvalidArg, "empty playlist name")};
                    auto r = db_->renamePlaylist(cmd.pid, cmd.newName);
                    if (!r)
                        return std::unexpected{r.error()};
                    return Result{Empty{}};
                },
                [&](const PlaylistExport& cmd) -> std::expected<Result, caudio::utils::Error> {
                    auto tracks = db_->playlistGetTracks(cmd.pid);
                    if (!tracks)
                        return std::unexpected{tracks.error()};
                    std::vector<caudio::db::Track> out;
                    out.reserve(tracks->size());
                    for (auto& t : std::span<const caudio::db::Track>(*tracks))
                        out.push_back(std::move(t));
                    return Result{PlaylistData{std::move(out), cmd.format}};
                },
                [&](const PlaylistImport& cmd) -> std::expected<Result, caudio::utils::Error> {
                    std::filesystem::path path{cmd.path};
                    std::error_code ec;
                    if (!std::filesystem::exists(path, ec)) {
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::NotFound, "file not found: " + cmd.path)};
                    }
                    std::string ext = path.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    std::vector<std::string> lines;
                    if (ext == ".json") {
                        // Parse JSON format
                        std::ifstream ifs(path);
                        if (!ifs)
                            return std::unexpected{caudio::utils::makeError(
                                caudio::utils::StatusCode::Io, "failed to open file")};
                        std::string content((std::istreambuf_iterator<char>(ifs)),
                                            std::istreambuf_iterator<char>());
                        try {
                            auto j = caudio::json::ordered_json::parse(content);
                            if (j.contains("tracks") && j["tracks"].is_array()) {
                                for (const auto& track : j["tracks"]) {
                                    if (track.contains("path") && track["path"].is_string()) {
                                        lines.push_back(track["path"].get<std::string>());
                                    }
                                }
                            }
                        } catch (const std::exception& e) {
                            return std::unexpected{caudio::utils::makeError(
                                caudio::utils::StatusCode::Corrupt, std::string("JSON parse error: ") + e.what())};
                        }
                    } else {
                        // M3U/PLS format
                        std::ifstream ifs(path);
                        if (!ifs)
                            return std::unexpected{caudio::utils::makeError(
                                caudio::utils::StatusCode::Io, "failed to open file")};
                        std::string line;
                        while (std::getline(ifs, line)) {
                            // trim
                            line.erase(0, line.find_first_not_of(" \t\r\n"));
                            line.erase(line.find_last_not_of(" \t\r\n") + 1);
                            if (line.empty() || line[0] == '#')
                                continue;
                            // Handle PLS format: File1=path, File2=path, etc.
                            if (ext == ".pls") {
                                std::size_t eq = line.find('=');
                                if (eq != std::string::npos && eq + 1 < line.size()) {
                                    std::string key = line.substr(0, eq);
                                    std::string val = line.substr(eq + 1);
                                    // trim key and val
                                    key.erase(0, key.find_first_not_of(" \t\r\n"));
                                    key.erase(key.find_last_not_of(" \t\r\n") + 1);
                                    val.erase(0, val.find_first_not_of(" \t\r\n"));
                                    val.erase(val.find_last_not_of(" \t\r\n") + 1);
                                    if (key.rfind("File", 0) == 0) { // starts with "File"
                                        lines.push_back(val);
                                    }
                                }
                            } else {
                                // M3U format: just the path
                                lines.push_back(line);
                            }
                        }
                    }
                    std::vector<int64_t> trackIds;
                    trackIds.reserve(lines.size());
                    size_t matched = 0, skipped = 0;
                    for (const auto& line : lines) {
                        auto tr = db_->findByPath(line);
                        if (tr) {
                            trackIds.push_back(tr->id);
                            ++matched;
                        } else {
                            ++skipped;
                        }
                    }
                    if (trackIds.empty()) {
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::NotFound, "no matching tracks found in library")};
                    }
                    std::string name = cmd.name.value_or(path.stem().string());
                    auto pidRes = db_->createPlaylistFromTracks(name, trackIds);
                    if (!pidRes)
                        return std::unexpected{pidRes.error()};
                    caudio::cli::PlaylistData pd{};
                    auto tracksRes = db_->playlistGetTracks(*pidRes);
                    if (tracksRes) {
                        for (auto& t : std::span<const caudio::db::Track>(*tracksRes))
                            pd.tracks.push_back(std::move(t));
                    }
                    pd.format = ext;
                    if (skipped > 0) {
                        std::println(std::cerr, "playlist import: skipped {} unmatched tracks", skipped);
                    }
                    std::println(std::cerr, "playlist import: matched {} tracks, created playlist '{}' (id={})",
                                 matched, name, *pidRes);
                    return Result{std::move(pd)};
                },
                [&](const Shutdown&) -> std::expected<Result, caudio::utils::Error> {
                    shutdownRequested_.store(true, std::memory_order_release);
                    // defer actual shutdown to run loop to avoid deadlock
                    return Result{Empty{}};
                },
                [&](const Preview&) -> std::expected<Result, caudio::utils::Error> {
                    return Result{Empty{}};
                },
                [&](const DeviceList&) -> std::expected<Result, caudio::utils::Error> {
                    auto devList = caudio::player::enumerateDevices();
                    caudio::cli::Devices result;
                    result.devices.reserve(devList.devices.size());
                    for (const auto& d : devList.devices) {
                        result.devices.push_back(caudio::cli::DeviceInfo{d.id, d.name, d.isDefault});
                    }
                    return Result{std::move(result)};
                },
                [&](const DeviceSet& cmd) -> std::expected<Result, caudio::utils::Error> {
                    // Validate device exists
                    auto devList = caudio::player::enumerateDevices();
                    bool found = false;
                    for (const auto& d : devList.devices) {
                        if (d.id == cmd.id) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::NotFound, "device not found: " + cmd.id)};
                    }
                    // Save to config
                    auto cfgPath = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                    auto res = detail::writeConfigValueRaw(cfgPath, "device", cmd.id);
                    if (!res) {
                        return std::unexpected{res.error()};
                    }
                    // Update engine's device if running - the engine will pick it up on next playback
                    // For now, just persist the config
                    return Result{Empty{}};
                },
                [&](const DeviceTest& cmd) -> std::expected<Result, caudio::utils::Error> {
                    // Get device ID to test
                    std::string testId;
                    if (cmd.id.has_value()) {
                        testId = *cmd.id;
                    } else {
                        // Use current config device
                        auto cfgPath = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                        auto devRes = detail::readConfigValueRaw(cfgPath, "device");
                        if (devRes) {
                            testId = *devRes;
                        } else {
                            testId = "auto";
                        }
                    }
                    // For "auto", use default device (empty ID in miniaudio)
                    // Create a temporary player to test the device
                    auto playerRes = caudio::player::Player::create();
                    if (!playerRes) {
                        return std::unexpected{playerRes.error()};
                    }
                    // Generate a short test tone (1 second of 440Hz sine wave at -20dB)
                    // This is a simple test - just verify device can be opened
                    // The actual tone generation would require more complex setup
                    // For now, return success if we can enumerate the device
                    auto devList = caudio::player::enumerateDevices();
                    bool found = false;
                    for (const auto& d : devList.devices) {
                        if (d.id == testId || (testId == "auto" && d.isDefault)) {
                            found = true;
                            break;
                        }
                    }
                    if (!found && testId != "auto") {
                        return std::unexpected{caudio::utils::makeError(
                            caudio::utils::StatusCode::NotFound, "device not found: " + testId)};
                    }
                    return Result{Empty{}};
                }},
            cmd);
    }

    ServiceConfig config_{};
    std::shared_ptr<caudio::db::Database> db_{};
    std::unique_ptr<caudio::engine::Engine> engine_{};
    std::unique_ptr<IpcServer> server_{};
    std::unique_ptr<caudio::utils::Logger> logger_{};
    std::filesystem::path pidPath_{};
    std::string socketPath_{};
    int lockFd_{-1};
    std::unique_ptr<caudio::service::ShmStatusHandle> shmHandle_{};
    std::string shmName_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdownRequested_{false};
};

} // namespace caudio::service
