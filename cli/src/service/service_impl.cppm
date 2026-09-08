module;
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#else
#include <process.h>
#endif

export module caudio.service:impl;

import caudio.utils;
import caudio.engine;
import caudio.db;
import caudio.cli;
import :ipc_channel;
import :ipc_server;

export namespace caudio::service {

struct ServiceConfig {
    std::filesystem::path dbPath{"library.db"};
    std::filesystem::path socketPath{};
    int logLevel{2};
};

namespace detail_svc {

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

inline std::filesystem::path pidPathForSocket(const std::filesystem::path& dbPath,
                                              const std::filesystem::path& socketPath) {
    if (!socketPath.empty()) {
#ifdef _WIN32
        std::string s = socketPath.generic_string();
        if (s.rfind("\\\\", 0) == 0 || s.rfind("//", 0) == 0) {
            // named pipe -> place pid next to db
            std::filesystem::path p = dbPath;
            if (p.empty()) {
                const char* home = std::getenv("HOME");
                if (!home || home[0] == '\0') home = std::getenv("USERPROFILE");
                std::filesystem::path base;
                if (home && home[0] != '\0') base = std::filesystem::path(home) / ".local" / "share" / "caudio";
                else base = std::filesystem::temp_directory_path() / "caudio";
                p = base / "caudio.db";
            }
            auto parent = p.parent_path();
            if (parent.empty()) parent = std::filesystem::current_path();
            return parent / "caudio.pid";
        }
#endif
        try {
            auto parent = socketPath.parent_path();
            if (parent.empty()) {
                // socketPath may be string like \\.\pipe\caudio-... on Windows - handled above
                // fallback to db parent
                auto pp = dbPath.parent_path();
                if (pp.empty()) pp = std::filesystem::current_path();
                return pp / "caudio.pid";
            }
            return parent / "caudio.pid";
        } catch (...) {
            auto pp = dbPath.parent_path();
            if (pp.empty()) pp = std::filesystem::current_path();
            return pp / "caudio.pid";
        }
    }
    auto pp = dbPath.parent_path();
    if (pp.empty()) pp = std::filesystem::current_path();
    return pp / "caudio.pid";
}

inline bool probeSocketAlive(const std::string& sp) {
#ifdef _WIN32
    // For named pipe, try to open it
    std::wstring w;
    w.reserve(sp.size());
    for (char c : sp) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    // Use CreateFileW to probe - if succeeds, someone is listening
    // Minimal import: use WinAPI via extern declared in ipc_server
    // We avoid direct WinAPI here to keep minimal C; just return false (stale assumed not alive)
    // Instead treat existence of file is not applicable for pipe.
    (void)w;
    return false;
#else
    if (sp.empty()) return false;
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (sp.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return false;
    }
    std::memcpy(addr.sun_path, sp.c_str(), sp.size() + 1);
    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::close(fd);
    return rc == 0;
#endif
}

inline std::expected<caudio::cli::Status, caudio::utils::Error>
buildStatus(caudio::engine::Engine& eng, caudio::db::Database& db) {
    caudio::cli::Status s{};
    s.state = eng.state();
    s.pos = eng.position();
    s.dur = eng.duration();
    s.vol = eng.volume();
    s.muted = false;
    s.shuffle = false;
    s.repeat = caudio::engine::RepeatMode::Off;
    s.trackId = eng.currentTrackId();
    // try to fetch shuffle/repeat from DB engine_state if possible
    // we approximate: query engine_state
    // but keep defaults if query fails
    // attempt to enrich track metadata
    if (s.trackId != 0) {
        auto tr = db.getTrack(s.trackId);
        if (tr) {
            s.title = tr->title;
            s.artist = tr->artist;
            s.path = tr->path;
        }
    }
    // queue size / index
    try {
        auto items = db.queueList(1);
        if (items) {
            s.qSize = items->size();
            // qIdx: find position of current track in queue? use 0 for now
            // If shuffle perm, not trivial. Keep 0.
            s.qIdx = 0;
            if (s.trackId != 0 && !items->empty()) {
                for (std::size_t i = 0; i < items->size(); ++i) {
                    if ((*items)[i].trackId == s.trackId) {
                        s.qIdx = i;
                        break;
                    }
                }
            }
        }
    } catch (...) {}
    return s;
}

} // namespace detail_svc

class Service final {
public:
    using ExpectedService = std::expected<std::unique_ptr<Service>, caudio::utils::Error>;

    static ExpectedService create(const ServiceConfig& cfg) {
        // stale detection: check socket alive
        std::string spStr;
        if (!cfg.socketPath.empty()) {
            spStr = cfg.socketPath.generic_string();
        } else {
            auto sp = socketPathFor(cfg.dbPath);
            if (sp) spStr = *sp;
        }
        if (!spStr.empty() && !spStr.starts_with("\\\\")) {
            std::filesystem::path sockP(spStr);
            std::error_code ec;
            if (std::filesystem::exists(sockP, ec)) {
                if (detail_svc::probeSocketAlive(spStr)) {
                    return std::unexpected{caudio::utils::makeError(caudio::utils::Result::AlreadyExists, "service already running")};
                } else {
                    // stale socket, remove
                    std::filesystem::remove(sockP, ec);
                }
            }
            // also check pid file
            auto pidPath = detail_svc::pidPathForSocket(cfg.dbPath, cfg.socketPath.empty() ? std::filesystem::path(spStr) : cfg.socketPath);
            if (std::filesystem::exists(pidPath, ec)) {
                // if socket not alive, pid is stale -> remove
                if (!detail_svc::probeSocketAlive(spStr)) {
                    std::filesystem::remove(pidPath, ec);
                } else {
                    // check if pid file contains live pid? we already probed socket, so keep
                }
            }
        }

        // open DB
        auto dbRes = caudio::db::Database::open(cfg.dbPath.generic_string());
        if (!dbRes) return std::unexpected{dbRes.error()};
        std::shared_ptr<caudio::db::Database> dbShared(std::move(dbRes.value()));

        // create Engine
        caudio::engine::EngineConfig ecfg{};
        // map logLevel to logger? EngineConfig doesn't have logLevel directly
        auto engRes = caudio::engine::Engine::create(ecfg);
        if (!engRes) return std::unexpected{engRes.error()};
        std::unique_ptr<caudio::engine::Engine> eng = std::move(engRes.value());
        if (auto e = eng->attachDatabase(dbShared); !e) {
            return std::unexpected{e.error()};
        }

        // logger
        caudio::utils::Logger logger(
            [](caudio::utils::Level lvl, std::string_view msg) {
                (void)lvl;
                (void)msg;
            },
            static_cast<caudio::utils::Level>(std::clamp(cfg.logLevel, 0, 3)));

        // ipc server listen
        auto srvPtr = std::make_unique<IpcServer>();
        auto listenRes = srvPtr->listen(cfg.dbPath);
        if (!listenRes) return std::unexpected{listenRes.error()};

        auto loggerPtr = std::make_unique<caudio::utils::Logger>(
            [](caudio::utils::Level lvl, std::string_view msg) {
                (void)lvl;
                (void)msg;
            },
            static_cast<caudio::utils::Level>(std::clamp(cfg.logLevel, 0, 3)));

        // pid file creation
        std::filesystem::path pidPath = detail_svc::pidPathForSocket(cfg.dbPath,
            cfg.socketPath.empty() ? std::filesystem::path(spStr) : cfg.socketPath);
        try {
            auto parent = pidPath.parent_path();
            if (!parent.empty()) {
                std::error_code ec2;
                std::filesystem::create_directories(parent, ec2);
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
        } catch (...) {}

        auto svc = std::unique_ptr<Service>(new Service(cfg, dbShared, std::move(eng), std::move(srvPtr), std::move(loggerPtr), pidPath, std::filesystem::path(spStr)));
        return svc;
    }

    ~Service() { shutdown(); }

    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    Service(Service&&) = delete;
    Service& operator=(Service&&) = delete;

    caudio::utils::Expected<void> run(std::stop_token st) {
        if (running_.exchange(true)) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::State, "already running")};
        }
        // build dispatcher
        auto dispatcher = [this](const caudio::cli::Command& cmd)
            -> std::expected<caudio::cli::Result, caudio::utils::Error> {
            return this->dispatch(cmd);
        };
        if (server_) server_->run(st, dispatcher);
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
        if (server_) server_->shutdown();
        if (engine_) engine_->shutdown();
        // cleanup pid file and socket
        try {
            std::error_code ec;
            if (!pidPath_.empty() && std::filesystem::exists(pidPath_, ec)) {
                std::filesystem::remove(pidPath_, ec);
            }
            if (!socketPath_.empty()) {
                std::string s = socketPath_.generic_string();
                if (!s.starts_with("\\\\") && !s.empty()) {
                    std::filesystem::path sp(s);
                    if (std::filesystem::exists(sp, ec)) {
                        // only unlink if we own it (server already did unlink on shutdown)
                        // keep attempt
                    }
                }
            }
        } catch (...) {}
    }

    caudio::db::Database& db() noexcept { return *db_; }
    caudio::engine::Engine& engine() noexcept { return *engine_; }
    IpcServer& server() noexcept { return *server_; }

private:
    Service(const ServiceConfig& cfg,
            std::shared_ptr<caudio::db::Database> db,
            std::unique_ptr<caudio::engine::Engine> eng,
            std::unique_ptr<IpcServer> srv,
            std::unique_ptr<caudio::utils::Logger> logger,
            std::filesystem::path pidPath,
            std::filesystem::path socketPath)
        : config_(cfg), db_(std::move(db)), engine_(std::move(eng)), server_(std::move(srv)), logger_(std::move(logger)),
          pidPath_(std::move(pidPath)), socketPath_(std::move(socketPath)) {}

    std::expected<caudio::cli::Result, caudio::utils::Error> dispatch(const caudio::cli::Command& cmd) {
        using namespace caudio::cli;
        // helper to build status
        auto statusResult = [&]() -> std::expected<Result, caudio::utils::Error> {
            auto st = detail_svc::buildStatus(*engine_, *db_);
            if (!st) return std::unexpected{st.error()};
            return Result{*st};
        };

        return std::visit(detail_svc::overloaded{
            [&](const Play&) -> std::expected<Result, caudio::utils::Error> {
                auto r = engine_->play(1);
                if (!r) return std::unexpected{r.error()};
                return statusResult();
            },
            [&](const Pause&) -> std::expected<Result, caudio::utils::Error> {
                auto r = engine_->pause();
                if (!r) return std::unexpected{r.error()};
                return statusResult();
            },
            [&](const Resume&) -> std::expected<Result, caudio::utils::Error> {
                auto r = engine_->resume();
                if (!r) return std::unexpected{r.error()};
                return statusResult();
            },
            [&](const Restart&) -> std::expected<Result, caudio::utils::Error> {
                // restart: seek to 0, ensure playing
                auto r = engine_->seek(0.0);
                if (!r) {
                    // if no track, try play
                    auto pr = engine_->play(1);
                    if (!pr) return std::unexpected{pr.error()};
                    return statusResult();
                }
                // ensure playing
                if (engine_->state() == caudio::engine::PlaybackState::Paused) {
                    (void)engine_->resume();
                }
                return statusResult();
            },
            [&](const Stop&) -> std::expected<Result, caudio::utils::Error> {
                auto r = engine_->stop();
                if (!r) return std::unexpected{r.error()};
                return statusResult();
            },
            [&](const Next&) -> std::expected<Result, caudio::utils::Error> {
                auto r = engine_->next();
                if (!r) return std::unexpected{r.error()};
                return statusResult();
            },
            [&](const Prev&) -> std::expected<Result, caudio::utils::Error> {
                auto r = engine_->prev();
                if (!r) return std::unexpected{r.error()};
                return statusResult();
            },
            [&](const Seek& s) -> std::expected<Result, caudio::utils::Error> {
                if (!std::isfinite(s.seconds) || s.seconds < 0) {
                    return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "seek: invalid seconds")};
                }
                auto r = engine_->seek(s.seconds);
                if (!r) return std::unexpected{r.error()};
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
                        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "volume: invalid level")};
                    }
                    // clamp 0-100 -> 0.0-1.0
                    if (lvl < 0.0f) lvl = 0.0f;
                    if (lvl > 100.0f) lvl = 100.0f;
                    target = lvl / 100.0f;
                    hasTarget = true;
                }
                if (v.deltaPct.has_value()) {
                    int d = *v.deltaPct;
                    float curPct = cur * 100.0f;
                    float np = curPct + static_cast<float>(d);
                    if (np < 0.0f) np = 0.0f;
                    if (np > 100.0f) np = 100.0f;
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
                    if (!r) return std::unexpected{r.error()};
                }
                caudio::cli::VolumeInfo vi{engine_->volume(), engine_->volume() == 0.0f};
                return Result{vi};
            },
            [&](const QueueList&) -> std::expected<Result, caudio::utils::Error> {
                int64_t qid = 1;
                // validation: queue exists
                {
                    auto q = db_->getQueue(qid);
                    if (!q) return std::unexpected{q.error()};
                }
                auto items = db_->queueList(qid);
                if (!items) return std::unexpected{items.error()};
                std::vector<caudio::db::Track> tracks;
                tracks.reserve(items->size());
                for (auto& it : *items) {
                    auto tr = db_->getTrack(it.trackId);
                    if (tr) tracks.push_back(std::move(*tr));
                }
                return Result{QueueTracks{std::move(tracks)}};
            },
            [&](const QueueQueues&) -> std::expected<Result, caudio::utils::Error> {
                auto qs = db_->listQueues();
                if (!qs) return std::unexpected{qs.error()};
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
                if (!q) return std::unexpected{q.error()};
                // For now just return status; engine queue switching not fully implemented
                // We store queueId in engine via play(qid) context? Keep simple.
                return statusResult();
            },
            [&](const QueueAdd& qa) -> std::expected<Result, caudio::utils::Error> {
                int64_t qid = 1;
                auto q = db_->getQueue(qid);
                if (!q) return std::unexpected{q.error()};
                std::vector<caudio::db::Track> toAdd;
                if (qa.search) {
                    auto sr = caudio::db::searchFts(*db_, qa.query, 50);
                    if (!sr) return std::unexpected{sr.error()};
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
                            for (std::size_t i = (s[0]=='-'?1:0); i < s.size(); ++i) if (!std::isdigit((unsigned char)s[i])) { isNum=false; break; }
                            if (isNum) {
                                id = std::stoll(s);
                                parsed = true;
                            }
                        }
                    } catch (...) {}
                    if (parsed && id != 0) {
                        auto tr = db_->getTrack(id);
                        if (!tr) return std::unexpected{tr.error()};
                        toAdd.push_back(std::move(*tr));
                    } else {
                        // try findByPath
                        auto tr = db_->findByPath(qa.query);
                        if (tr) {
                            toAdd.push_back(std::move(*tr));
                        } else {
                            // fallback to search LIKE
                            auto sr = caudio::db::searchLike(*db_, qa.query, 10);
                            if (!sr) return std::unexpected{sr.error()};
                            toAdd = std::move(*sr);
                            if (toAdd.empty()) {
                                return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound, "track not found: " + qa.query)};
                            }
                        }
                    }
                }
                for (auto& t : toAdd) {
                    auto er = db_->queueEnqueue(qid, t.id);
                    if (!er) return std::unexpected{er.error()};
                }
                // return updated queue
                auto items = db_->queueList(qid);
                if (!items) return std::unexpected{items.error()};
                std::vector<caudio::db::Track> tracks;
                tracks.reserve(items->size());
                for (auto& it : *items) {
                    auto tr = db_->getTrack(it.trackId);
                    if (tr) tracks.push_back(std::move(*tr));
                }
                return Result{QueueTracks{std::move(tracks)}};
            },
            [&](const QueueRemove& qr) -> std::expected<Result, caudio::utils::Error> {
                int64_t qid = 1;
                auto q = db_->getQueue(qid);
                if (!q) return std::unexpected{q.error()};
                // parse idOrIndex
                int64_t val = 0;
                bool isNum = false;
                try {
                    std::string s = qr.idOrIndex;
                    s.erase(0, s.find_first_not_of(" \t\n\r"));
                    s.erase(s.find_last_not_of(" \t\n\r") + 1);
                    if (!s.empty()) {
                        bool allDigit = true;
                        std::size_t off = (s[0]=='-'?1:0);
                        for (std::size_t i=off;i<s.size();++i) if (!std::isdigit((unsigned char)s[i])) { allDigit=false; break; }
                        if (allDigit) { val = std::stoll(s); isNum = true; }
                    }
                } catch (...) {}
                if (!isNum) {
                    return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "invalid idOrIndex")};
                }
                // try as position first
                auto rm = db_->queueRemove(qid, val);
                if (!rm) {
                    // if not found as position, try as trackId lookup
                    if (rm.error().code == caudio::utils::Result::NotFound) {
                        auto items = db_->queueList(qid);
                        if (!items) return std::unexpected{items.error()};
                        bool found = false;
                        int64_t pos = -1;
                        for (auto& it : *items) if (it.trackId == val) { pos = it.position; found = true; break; }
                        if (!found) return std::unexpected{rm.error()};
                        auto rm2 = db_->queueRemove(qid, pos);
                        if (!rm2) return std::unexpected{rm2.error()};
                    } else {
                        return std::unexpected{rm.error()};
                    }
                }
                auto items = db_->queueList(qid);
                if (!items) return std::unexpected{items.error()};
                std::vector<caudio::db::Track> tracks;
                for (auto& it : *items) {
                    auto tr = db_->getTrack(it.trackId);
                    if (tr) tracks.push_back(std::move(*tr));
                }
                return Result{QueueTracks{std::move(tracks)}};
            },
            [&](const QueueMove& qm) -> std::expected<Result, caudio::utils::Error> {
                // QueueMove: reorder within queue via playlistReorder? For queue we lack direct move.
                // Simulate via remove+enqueue: fetch items, reorder vector, clear and re-enqueue
                int64_t qid = 1;
                auto q = db_->getQueue(qid);
                if (!q) return std::unexpected{q.error()};
                auto items = db_->queueList(qid);
                if (!items) return std::unexpected{items.error()};
                if (qm.from >= items->size() || qm.to >= items->size()) {
                    return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "move out of range")};
                }
                // collect trackIds in order
                std::vector<int64_t> ids;
                ids.reserve(items->size());
                for (auto& it : *items) ids.push_back(it.trackId);
                int64_t mv = ids[qm.from];
                ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(qm.from));
                ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(qm.to), mv);
                // clear and re-add
                (void)db_->queueClear(qid);
                for (auto id : ids) (void)db_->queueEnqueue(qid, id);
                auto nitems = db_->queueList(qid);
                if (!nitems) return std::unexpected{nitems.error()};
                std::vector<caudio::db::Track> tracks;
                for (auto& it : *nitems) {
                    auto tr = db_->getTrack(it.trackId);
                    if (tr) tracks.push_back(std::move(*tr));
                }
                return Result{QueueTracks{std::move(tracks)}};
            },
            [&](const QueueClear&) -> std::expected<Result, caudio::utils::Error> {
                int64_t qid = 1;
                auto q = db_->getQueue(qid);
                if (!q) return std::unexpected{q.error()};
                auto r = db_->queueClear(qid);
                if (!r) return std::unexpected{r.error()};
                return Result{QueueTracks{{}}};
            },
            [&](const QueueShuffle& qs) -> std::expected<Result, caudio::utils::Error> {
                bool on = qs.on.value_or(false);
                // if on not provided, toggle? default to true for now
                if (!qs.on.has_value()) {
                    // toggle: we don't have getter, just enable
                    on = true;
                }
                auto r = engine_->setShuffle(on);
                if (!r) return std::unexpected{r.error()};
                return statusResult();
            },
            [&](const QueueRepeat& qr) -> std::expected<Result, caudio::utils::Error> {
                caudio::engine::RepeatMode m = qr.mode.value_or(caudio::engine::RepeatMode::Off);
                auto r = engine_->setRepeat(m);
                if (!r) return std::unexpected{r.error()};
                return statusResult();
            },
            [&](const LibraryScan& ls) -> std::expected<Result, caudio::utils::Error> {
                // stub: if path provided scan, else stats
                (void)ls;
                return Result{caudio::utils::Error{caudio::utils::Result::Unsupported, "LibraryScan not implemented"}};
            },
            [&](const LibrarySearch& ls) -> std::expected<Result, caudio::utils::Error> {
                auto sr = caudio::db::searchFts(*db_, ls.query, ls.limit);
                if (!sr) return std::unexpected{sr.error()};
                return Result{Tracks{std::move(*sr)}};
            },
            [&](const LibraryStats&) -> std::expected<Result, caudio::utils::Error> {
                auto st = db_->getStats();
                if (!st) return std::unexpected{st.error()};
                caudio::cli::LibraryStatsData d{};
                d.tracks = static_cast<std::size_t>(st->num_tracks);
                d.queues = static_cast<std::size_t>(st->num_queue_items);
                d.playlists = static_cast<std::size_t>(st->num_playlists);
                return Result{d};
            },
            [&](const ConfigGet& cg) -> std::expected<Result, caudio::utils::Error> {
                (void)cg;
                return Result{Empty{}};
            },
            [&](const ConfigSet& cs) -> std::expected<Result, caudio::utils::Error> {
                (void)cs;
                return Result{Empty{}};
            },
            [&](const ConfigList&) -> std::expected<Result, caudio::utils::Error> {
                return Result{Empty{}};
            },
            [&](const ConfigExport& ce) -> std::expected<Result, caudio::utils::Error> {
                (void)ce;
                return Result{Empty{}};
            },
            [&](const ConfigImport& ci) -> std::expected<Result, caudio::utils::Error> {
                (void)ci;
                return Result{Empty{}};
            },
            [&](const PlaylistList&) -> std::expected<Result, caudio::utils::Error> {
                return Result{Empty{}};
            },
            [&](const PlaylistTracks&) -> std::expected<Result, caudio::utils::Error> {
                return Result{Empty{}};
            },
            [&](const PlaylistLoad&) -> std::expected<Result, caudio::utils::Error> {
                return Result{Empty{}};
            },
            [&](const PlaylistSave&) -> std::expected<Result, caudio::utils::Error> {
                return Result{Empty{}};
            },
            [&](const PlaylistDelete&) -> std::expected<Result, caudio::utils::Error> {
                return Result{Empty{}};
            },
            [&](const Shutdown&) -> std::expected<Result, caudio::utils::Error> {
                shutdownRequested_.store(true, std::memory_order_release);
                // defer actual shutdown to run loop to avoid deadlock
                return Result{Empty{}};
            },
            [&](const Preview&) -> std::expected<Result, caudio::utils::Error> {
                return Result{Empty{}};
            }
        }, cmd);
    }

    ServiceConfig config_{};
    std::shared_ptr<caudio::db::Database> db_{};
    std::unique_ptr<caudio::engine::Engine> engine_{};
    std::unique_ptr<IpcServer> server_{};
    std::unique_ptr<caudio::utils::Logger> logger_{};
    std::filesystem::path pidPath_{};
    std::filesystem::path socketPath_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdownRequested_{false};
};

} // namespace caudio::service
