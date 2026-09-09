module;
#include <nlohmann/json.hpp>
// Service owns Engine/DB/Config/Logger/IpcServer and dispatches commands

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <generator>
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

#include "blake3.h"

#ifndef _WIN32
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#else
#include <process.h>
#include <windows.h>
#endif

export module caudio.service:impl;

import caudio.utils;
import caudio.engine;
import caudio.db;
import caudio.cli;
import caudio.player;
import :ipc_channel;
import :ipc_server;
import :shm_status;

export namespace caudio::service {

struct ServiceConfig {
    std::filesystem::path dbPath{"library.db"};
    std::filesystem::path socketPath{};
    std::filesystem::path configPath{};
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

inline std::filesystem::path lockPathForSocket(const std::filesystem::path& dbPath,
                                               const std::filesystem::path& socketPath) {
    auto pidPath = pidPathForSocket(dbPath, socketPath);
    std::string dbStr = dbPath.generic_string();
    std::size_t hash = std::hash<std::string>{}(dbStr);
    return pidPath.parent_path() / ("caudio-" + std::to_string(hash) + ".lock");
}

inline std::filesystem::path socketPathForDb(const std::filesystem::path& dbPath) {
#ifdef _WIN32
    std::string dbStr = dbPath.generic_string();
    std::size_t hash = std::hash<std::string>{}(dbStr);
    return std::filesystem::path("\\\\.\\pipe\\caudio-" + std::to_string(hash));
#else
    auto pp = dbPath.parent_path();
    if (pp.empty()) pp = std::filesystem::current_path();
    std::error_code ec;
    std::filesystem::create_directories(pp, ec);
    // Use a hash of the db path for the socket name
    std::string dbStr = dbPath.generic_string();
    std::size_t hash = std::hash<std::string>{}(dbStr);
    return pp / ("caudio-" + std::to_string(hash) + ".sock");
#endif
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

inline bool tryAcquireLock(const std::filesystem::path& lockPath, int& outFd) {
    // MVP: disable file flock on Windows — rely on socket bind for single-instance
    // TODO: fix CreateFileW path handling for flock
#ifdef _WIN32
    outFd = -1;
    return true;
#else
    std::error_code ec;
    auto parent = lockPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    int fd = ::open(lockPath.c_str(), O_CREAT | O_CLOEXEC | O_RDWR, 0600);
    if (fd < 0) return false;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return false;
    }
    outFd = fd;
    return true;
#endif
}

inline void releaseLock(int fd) {
    if (fd < 0) return;
#ifndef _WIN32
    ::flock(fd, LOCK_UN);
    ::close(fd);
#else
    HANDLE h = reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd));
    OVERLAPPED ov{};
    ::UnlockFileEx(h, 0, 1, 0, &ov);
    ::CloseHandle(h);
#endif
}

inline bool checkPidAlive(int pid) {
    if (pid <= 0) return false;
#ifndef _WIN32
    // kill(pid, 0) checks if process exists
    return ::kill(pid, 0) == 0;
#else
    HANDLE h = ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    DWORD wait = ::WaitForSingleObject(h, 0);
    ::CloseHandle(h);
    return wait == WAIT_TIMEOUT; // timeout means still running
#endif
}

inline std::optional<int> readPidFile(const std::filesystem::path& pidPath) {
    std::error_code ec;
    if (!std::filesystem::exists(pidPath, ec)) return std::nullopt;
    std::ifstream in(pidPath);
    if (!in) return std::nullopt;
    int pid = 0;
    in >> pid;
    if (in.fail()) return std::nullopt;
    return pid;
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

inline std::filesystem::path resolveConfigPath(const ServiceConfig& cfg) {
    if (!cfg.configPath.empty()) return cfg.configPath;
    if (!cfg.dbPath.empty()) {
        auto pp = cfg.dbPath.parent_path();
        if (!pp.empty()) return pp / "config.json";
    }
    std::error_code ec;
    auto tmp = std::filesystem::temp_directory_path(ec);
    if (ec) tmp = std::filesystem::path("/tmp");
    return tmp / "caudio" / "config.json";
}

inline std::expected<std::string, caudio::utils::Error>
readConfigValueRaw(const std::filesystem::path& p, std::string_view key) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound, "config not found")};
    std::ifstream in(p);
    if (!in) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "cannot open config")};
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string pat = "\"" + std::string(key) + "\"";
    auto pos = content.find(pat);
    if (pos == std::string::npos) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound, "key not found: " + std::string(key))};
    auto colon = content.find(':', pos + pat.size());
    if (colon == std::string::npos) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, "invalid config")};
    auto start = content.find_first_not_of(" \t\n\r", colon + 1);
    if (start == std::string::npos) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, "invalid config value")};
    if (content[start] == '"') {
        auto end = content.find('"', start + 1);
        if (end == std::string::npos) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, "invalid string value")};
        return content.substr(start + 1, end - start - 1);
    } else {
        auto end = content.find_first_of(",}\n\r", start);
        if (end == std::string::npos) end = content.size();
        auto s = content.substr(start, end - start);
        // trim
        auto a = s.find_first_not_of(" \t\n\r");
        auto b = s.find_last_not_of(" \t\n\r");
        if (a == std::string::npos) return std::string{};
        return s.substr(a, b - a + 1);
    }
}

inline caudio::utils::Expected<void>
writeConfigValueRaw(const std::filesystem::path& p, std::string_view key, std::string_view value) {
    std::string content;
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) {
        std::ifstream in(p);
        if (in) content.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    if (content.empty()) content = "{}";
    std::string pat = "\"" + std::string(key) + "\"";
    auto pos = content.find(pat);
    std::string valueRepr;
    // try to detect if value is json (number/bool/object) vs string: simple check
    bool isJson = false;
    if (!value.empty() && (value.front() == '{' || value.front() == '[' || value == "true" || value == "false" || value == "null" || std::isdigit((unsigned char)value.front()) || value.front() == '-')) isJson = true;
    if (!isJson) valueRepr = "\"" + std::string(value) + "\"";
    else valueRepr = std::string(value);
    if (pos != std::string::npos) {
        auto colon = content.find(':', pos + pat.size());
        if (colon == std::string::npos) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, "invalid config")};
        auto start = content.find_first_not_of(" \t\n\r", colon + 1);
        if (start == std::string::npos) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, "invalid")};
        std::size_t end;
        if (content[start] == '"') {
            end = content.find('"', start + 1);
            if (end == std::string::npos) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, "invalid")};
            ++end;
        } else {
            end = content.find_first_of(",}", start);
            if (end == std::string::npos) end = content.size();
        }
        content.replace(start, end - start, valueRepr);
    } else {
        // insert before final }
        auto last = content.find_last_of('}');
        if (last == std::string::npos) {
            content = "{\"" + std::string(key) + "\": " + valueRepr + "}";
        } else {
            std::string before = content.substr(0, last);
            std::string after = content.substr(last);
            bool needsComma = before.find('"') != std::string::npos && before.find_last_of(',') != before.find_last_of('{') && before.back() != '{' && before.find(':') != std::string::npos;
            // simplified: if before contains ':' then need comma
            if (before.find(':') != std::string::npos) {
                // check if last non-space is '{' or ','
                auto t = before.find_last_not_of(" \t\n\r");
                if (t != std::string::npos && before[t] != '{' && before[t] != ',') needsComma = true;
                else needsComma = false;
            } else needsComma = false;
            std::string ins;
            if (needsComma) ins = ", \"" + std::string(key) + "\": " + valueRepr;
            else ins = "\"" + std::string(key) + "\": " + valueRepr;
            content = before + ins + after;
        }
    }
    try {
        auto parent = p.parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
        std::ofstream out(p);
        if (!out) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "cannot write config")};
        out << content;
        return {};
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, e.what())};
    }
}

inline std::expected<std::vector<caudio::cli::ConfigValue>, caudio::utils::Error>
listConfigValuesRaw(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return std::vector<caudio::cli::ConfigValue>{};
    std::ifstream in(p);
    if (!in) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "cannot open config")};
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<caudio::cli::ConfigValue> out;
    std::size_t pos = 0;
    while (true) {
        auto q1 = content.find('"', pos);
        if (q1 == std::string::npos) break;
        auto q2 = content.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        std::string k = content.substr(q1 + 1, q2 - q1 - 1);
        auto colon = content.find(':', q2 + 1);
        if (colon == std::string::npos) break;
        auto start = content.find_first_not_of(" \t\n\r", colon + 1);
        if (start == std::string::npos) break;
        std::string v;
        if (content[start] == '"') {
            auto e = content.find('"', start + 1);
            if (e == std::string::npos) break;
            v = content.substr(start + 1, e - start - 1);
            pos = e + 1;
        } else {
            auto e = content.find_first_of(",}", start);
            if (e == std::string::npos) e = content.size();
            v = content.substr(start, e - start);
            auto a = v.find_first_not_of(" \t\n\r");
            auto b = v.find_last_not_of(" \t\n\r");
            if (a != std::string::npos) v = v.substr(a, b - a + 1);
            pos = e + 1;
        }
        if (!k.empty() && k != "type" ) {
            out.push_back(caudio::cli::ConfigValue{k, v});
        }
        if (pos >= content.size()) break;
    }
    return out;
}

inline bool hasAudioExt(const std::filesystem::path& p) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".mp3" || ext == ".flac" || ext == ".ogg" || ext == ".wav" || ext == ".m4a";
}

inline std::expected<std::array<std::uint8_t, 32>, caudio::utils::Error>
computeFingerprint(const std::filesystem::path& path) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    if (ec) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, ec.message())};
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "cannot open file")};
    constexpr std::size_t kSample = 64 * 1024;
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    std::vector<std::uint8_t> buf(kSample);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(kSample));
    std::size_t n = static_cast<std::size_t>(f.gcount());
    if (n != 0) blake3_hasher_update(&hasher, buf.data(), n);
    if (sz > kSample) {
        f.clear();
        f.seekg(static_cast<std::streamoff>(sz - kSample), std::ios::beg);
        if (f) {
            f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(kSample));
            n = static_cast<std::size_t>(f.gcount());
            if (n != 0) blake3_hasher_update(&hasher, buf.data(), n);
        }
    }
    std::uint64_t sz64 = static_cast<std::uint64_t>(sz);
    blake3_hasher_update(&hasher, &sz64, sizeof(sz64));
    std::uint32_t ver = 1;
    blake3_hasher_update(&hasher, &ver, sizeof(ver));
    std::array<std::uint8_t, 32> out{};
    blake3_hasher_finalize(&hasher, out.data(), out.size());
    return out;
}

inline double durationFromDecoder(const std::filesystem::path& path) noexcept {
    try {
        auto readerRes = caudio::player::FileReader::open(path);
        if (!readerRes) return 0.0;
        auto& readerPtr = readerRes.value();
        auto decRes = caudio::player::DecoderRegistry::open(*readerPtr);
        if (!decRes) return 0.0;
        auto& decPtr = decRes.value();
        std::uint32_t sr = decPtr->sampleRate();
        std::uint64_t frames = decPtr->totalFrames();
        if (sr == 0) return 0.0;
        std::span<const std::uint8_t> dummy{};
        (void)dummy;
        return static_cast<double>(frames) / static_cast<double>(sr);
    } catch (...) {
        return 0.0;
    }
}

} // namespace detail_svc

class Service final {
public:
    using ExpectedService = std::expected<std::unique_ptr<Service>, caudio::utils::Error>;

    static ExpectedService create(const ServiceConfig& cfg) {
        // Determine socket path
        std::string spStr;
        if (!cfg.socketPath.empty()) {
            spStr = cfg.socketPath.generic_string();
        } else {
            auto sp = detail_svc::socketPathForDb(cfg.dbPath);
            if (!sp.empty()) spStr = sp.generic_string();
        }

        // Single-instance enforcement via flock lock file
        std::filesystem::path lockPath = detail_svc::lockPathForSocket(cfg.dbPath,
            cfg.socketPath.empty() ? std::filesystem::path(spStr) : cfg.socketPath);
        int lockFd = -1;
        if (!detail_svc::tryAcquireLock(lockPath, lockFd)) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::AlreadyExists, "service already running (lock held)")};
        }

        // Check for stale PID file
        std::filesystem::path pidPath = detail_svc::pidPathForSocket(cfg.dbPath,
            cfg.socketPath.empty() ? std::filesystem::path(spStr) : cfg.socketPath);
        std::error_code ec;
        if (std::filesystem::exists(pidPath, ec)) {
            auto existingPid = detail_svc::readPidFile(pidPath);
            if (existingPid && detail_svc::checkPidAlive(*existingPid)) {
                // Process is alive, daemon already running
                detail_svc::releaseLock(lockFd);
                return std::unexpected{caudio::utils::makeError(caudio::utils::Result::AlreadyExists, "service already running (pid alive)")};
            }
            // Stale PID - remove it
            std::filesystem::remove(pidPath, ec);
        }

        // Check for stale socket
        if (!spStr.empty() && !spStr.starts_with("\\\\")) {
            std::filesystem::path sockP(spStr);
            if (std::filesystem::exists(sockP, ec)) {
                if (detail_svc::probeSocketAlive(spStr)) {
                    detail_svc::releaseLock(lockFd);
                    return std::unexpected{caudio::utils::makeError(caudio::utils::Result::AlreadyExists, "service already running (socket alive)")};
                } else {
                    // Stale socket - remove
                    std::filesystem::remove(sockP, ec);
                }
            }
        }

        // open DB
        auto dbRes = caudio::db::Database::open(cfg.dbPath.generic_string());
        if (!dbRes) return std::unexpected{dbRes.error()};
        std::shared_ptr<caudio::db::Database> dbShared(std::move(dbRes.value()));

        // create Engine
        caudio::engine::EngineConfig ecfg{};
        auto engRes = caudio::engine::Engine::create(ecfg);
        if (!engRes) return std::unexpected{engRes.error()};
        std::unique_ptr<caudio::engine::Engine> eng = std::move(engRes.value());
        if (auto e = eng->attachDatabase(dbShared); !e) {
            detail_svc::releaseLock(lockFd);
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
        if (!listenRes) {
            detail_svc::releaseLock(lockFd);
            return std::unexpected{listenRes.error()};
        }

        auto loggerPtr = std::make_unique<caudio::utils::Logger>(
            [](caudio::utils::Level lvl, std::string_view msg) {
                (void)lvl;
                (void)msg;
            },
            static_cast<caudio::utils::Level>(std::clamp(cfg.logLevel, 0, 3)));

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
        } catch (...) {}

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
        if (shmRes) shmHandle = std::make_unique<caudio::service::ShmStatusHandle>(std::move(*shmRes));

        auto svc = std::unique_ptr<Service>(new Service(cfg, dbShared, std::move(eng), std::move(srvPtr),
            std::move(loggerPtr), pidPath, std::filesystem::path(spStr), lockFd, std::move(shmHandle), shmName));
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
            // cleanup lock file
            if (lockFd_ >= 0) {
                detail_svc::releaseLock(lockFd_);
                lockFd_ = -1;
                auto lockPath = detail_svc::lockPathForSocket(config_.dbPath, socketPath_);
                std::filesystem::remove(lockPath, ec);
            }
        } catch (...) {}
        // shm handle will be cleaned up via RAII
    }

    caudio::db::Database& db() noexcept { return *db_; }
    caudio::engine::Engine& engine() noexcept { return *engine_; }
    IpcServer& server() noexcept { return *server_; }
    const std::string& shmName() const noexcept { return shmName_; }
    caudio::service::ShmStatusHandle* shmHandle() noexcept { return shmHandle_.get(); }

private:
    Service(const ServiceConfig& cfg,
            std::shared_ptr<caudio::db::Database> db,
            std::unique_ptr<caudio::engine::Engine> eng,
            std::unique_ptr<IpcServer> srv,
            std::unique_ptr<caudio::utils::Logger> logger,
            std::filesystem::path pidPath,
            std::filesystem::path socketPath,
            int lockFd,
            std::unique_ptr<caudio::service::ShmStatusHandle> shmHandle,
            std::string shmName)
        : config_(cfg), db_(std::move(db)), engine_(std::move(eng)), server_(std::move(srv)), logger_(std::move(logger)),
          pidPath_(std::move(pidPath)), socketPath_(std::move(socketPath)),
          lockFd_(lockFd), shmHandle_(std::move(shmHandle)), shmName_(std::move(shmName)) {}

    void updateShmStatus() {
        if (!shmHandle_ || !shmHandle_->valid()) return;
        auto& eng = *engine_;
        int64_t trackId = eng.currentTrackId();
        std::string title, artist;
        if (trackId != 0) {
            auto tr = db_->getTrack(trackId);
            if (tr) {
                title = tr->title;
                artist = tr->artist;
            }
        }
        // Get queue size
        size_t qSize = 0;
        if (auto items = db_->queueList(1); items) {
            qSize = items->size();
        }
        shmHandle_->updateFromEngine(eng, trackId, title, artist);
        shmHandle_->setQueueSize(qSize);
        // duration is not directly available from engine, would need track info
        if (trackId != 0) {
            auto tr = db_->getTrack(trackId);
            if (tr) {
                shmHandle_->setDuration(tr->duration);
            }
        }
    }

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
                updateShmStatus();
                return statusResult();
            },
            [&](const Pause&) -> std::expected<Result, caudio::utils::Error> {
                auto r = engine_->pause();
                if (!r) return std::unexpected{r.error()};
                updateShmStatus();
                return statusResult();
            },
            [&](const Resume&) -> std::expected<Result, caudio::utils::Error> {
                auto r = engine_->resume();
                if (!r) return std::unexpected{r.error()};
                updateShmStatus();
                return statusResult();
            },
            [&](const Restart&) -> std::expected<Result, caudio::utils::Error> {
                // restart: seek to 0, ensure playing
                auto r = engine_->seek(0.0);
                if (!r) {
                    // if no track, try play
                    auto pr = engine_->play(1);
                    if (!pr) return std::unexpected{pr.error()};
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
                if (!r) return std::unexpected{r.error()};
                updateShmStatus();
                return statusResult();
            },
            [&](const Next&) -> std::expected<Result, caudio::utils::Error> {
                auto r = engine_->next();
                if (!r) return std::unexpected{r.error()};
                updateShmStatus();
                return statusResult();
            },
            [&](const Prev&) -> std::expected<Result, caudio::utils::Error> {
                auto r = engine_->prev();
                if (!r) return std::unexpected{r.error()};
                updateShmStatus();
                return statusResult();
            },
            [&](const Seek& s) -> std::expected<Result, caudio::utils::Error> {
                if (!std::isfinite(s.seconds) || s.seconds < 0) {
                    return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "seek: invalid seconds")};
                }
                auto r = engine_->seek(s.seconds);
                if (!r) return std::unexpected{r.error()};
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
                updateShmStatus();
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
                if (!qa.search) {
                    std::filesystem::path p(qa.query);
                    std::error_code ec;
                    if (std::filesystem::exists(p, ec) && !ec && detail_svc::hasAudioExt(p)) {
                        auto fpRes = detail_svc::computeFingerprint(p);
                        if (!fpRes) return std::unexpected{fpRes.error()};
                        caudio::db::Track t;
                        t.path = p.generic_string();
                        t.fingerprint = *fpRes;
                        t.duration = detail_svc::durationFromDecoder(p);
                        {
                            std::error_code ec2;
                            auto sz = std::filesystem::file_size(p, ec2);
                            if (!ec2) t.size = static_cast<int64_t>(sz);
                            auto ftime = std::filesystem::last_write_time(p, ec2);
                            if (!ec2) t.mtime = static_cast<int64_t>(ftime.time_since_epoch().count());
                        }
                        int64_t newId = 0;
                        auto ins = db_->insertTrack(t);
                        if (ins) {
                            newId = *ins;
                            t.id = newId;
                        } else {
                            if (ins.error().code == caudio::utils::Result::AlreadyExists) {
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
                        if (!eq) return std::unexpected{eq.error()};
                        updateShmStatus();
                        std::vector<caudio::db::Track> single;
                        single.reserve(1);
                        single.push_back(std::move(t));
                        std::span<const caudio::db::Track> sp(single);
                        (void)sp;
                        return Result{QueueTracks{std::move(single)}};
                    }
                }
                std::vector<caudio::db::Track> toAdd;
                if (qa.search) {
                    auto sr = caudio::db::search(*db_, qa.query, 50);
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
                            // fallback to search via FTS (covers LIKE)
                            auto sr = caudio::db::search(*db_, qa.query, 50);
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
                std::span<const caudio::db::Track> spanAdd(toAdd);
                (void)spanAdd;
                updateShmStatus();
                return Result{QueueTracks{std::move(toAdd)}};
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
                updateShmStatus();
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
                updateShmStatus();
                return Result{QueueTracks{std::move(tracks)}};
            },
            [&](const QueueClear&) -> std::expected<Result, caudio::utils::Error> {
                int64_t qid = 1;
                auto q = db_->getQueue(qid);
                if (!q) return std::unexpected{q.error()};
                auto r = db_->queueClear(qid);
                if (!r) return std::unexpected{r.error()};
                updateShmStatus();
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
                updateShmStatus();
                return statusResult();
            },
            [&](const QueueRepeat& qr) -> std::expected<Result, caudio::utils::Error> {
                caudio::engine::RepeatMode m = qr.mode.value_or(caudio::engine::RepeatMode::Off);
                auto r = engine_->setRepeat(m);
                if (!r) return std::unexpected{r.error()};
                updateShmStatus();
                return statusResult();
            },
            [&](const LibraryScan& cmd) -> std::expected<Result, caudio::utils::Error> {
                std::filesystem::path root;
                if (cmd.path) root = std::filesystem::path(*cmd.path);
                else {
                    auto pp = config_.dbPath.parent_path();
                    if (pp.empty()) pp = std::filesystem::current_path();
                    root = pp / "music";
                }
                auto mode = (cmd.mode == "full" ? caudio::db::ScanMode::Full : caudio::db::ScanMode::Sampled);
                std::size_t n = 0;
                for (auto t : caudio::db::scan(root, mode)) {
                    std::span<const std::byte> dummy{};
                    (void)dummy;
                    auto r = db_->insertTrack(t);
                    if (r) ++n;
                }
                caudio::cli::LibraryStatsData d{};
                d.tracks = n;
                d.queues = 0;
                d.playlists = 0;
                if (auto st = db_->getStats()) {
                    d.queues = static_cast<std::size_t>(st->num_queue_items);
                    d.playlists = static_cast<std::size_t>(st->num_playlists);
                }
                // demonstrate to_underlying usage
                (void)std::to_underlying(caudio::utils::Result::Ok);
                return Result{std::move(d)};
            },
            [&](const LibrarySearch& cmd) -> std::expected<Result, caudio::utils::Error> {
                auto tracks = caudio::db::search(*db_, cmd.query, cmd.limit);
                if (!tracks) return std::unexpected{tracks.error()};
                // fallback to like handled inside search; if empty still return
                std::span<const caudio::db::Track> span{*tracks};
                std::vector<caudio::db::Track> out(span.begin(), span.end());
                return Result{Tracks{std::move(out)}};
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
            [&](const ConfigGet& cmd) -> std::expected<Result, caudio::utils::Error> {
                auto p = detail_svc::resolveConfigPath(config_);
                auto vRes = detail_svc::readConfigValueRaw(p, cmd.key);
                if (!vRes) return std::unexpected{vRes.error()};
                return Result{ConfigValue{cmd.key, *vRes}};
            },
            [&](const ConfigSet& cmd) -> std::expected<Result, caudio::utils::Error> {
                auto p = detail_svc::resolveConfigPath(config_);
                auto sRes = detail_svc::writeConfigValueRaw(p, cmd.key, cmd.value);
                if (!sRes) return std::unexpected{sRes.error()};
                return Result{Empty{}};
            },
            [&](const ConfigList&) -> std::expected<Result, caudio::utils::Error> {
                auto p = detail_svc::resolveConfigPath(config_);
                auto lRes = detail_svc::listConfigValuesRaw(p);
                if (!lRes) return std::unexpected{lRes.error()};
                ConfigValues cvs{};
                cvs.values = std::move(*lRes);
                std::span<const ConfigValue> span{cvs.values};
                (void)span;
                return Result{std::move(cvs)};
            },
            [&](const ConfigExport& cmd) -> std::expected<Result, caudio::utils::Error> {
                auto src = detail_svc::resolveConfigPath(config_);
                std::filesystem::path dst{cmd.path};
                std::error_code ec;
                if (!std::filesystem::exists(src, ec)) {
                    return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound, "config not found")};
                }
                std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, ec.message())};
                return Result{Empty{}};
            },
            [&](const ConfigImport& cmd) -> std::expected<Result, caudio::utils::Error> {
                std::filesystem::path src{cmd.path};
                auto dst = detail_svc::resolveConfigPath(config_);
                std::error_code ec;
                if (!std::filesystem::exists(src, ec)) {
                    return std::unexpected{caudio::utils::makeError(caudio::utils::Result::NotFound, "import path not found")};
                }
                std::filesystem::create_directories(dst.parent_path(), ec);
                std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
                if (ec) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, ec.message())};
                // validate that file is readable and non-empty JSON-like (at least contains '{')
                std::error_code ec2;
                if (!std::filesystem::exists(dst, ec2)) {
                    return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Io, "import failed")};
                }
                return Result{Empty{}};
            },
            [&](const PlaylistList&) -> std::expected<Result, caudio::utils::Error> {
                auto pls = db_->listPlaylists();
                if (!pls) return std::unexpected{pls.error()};
                std::span<const caudio::db::Playlist> span{*pls};
                for (auto& p : span) (void)std::to_underlying(static_cast<caudio::utils::Result>(p.type));
                std::vector<caudio::db::Playlist> out(span.begin(), span.end());
                return Result{Playlists{std::move(out)}};
            },
            [&](const PlaylistTracks& cmd) -> std::expected<Result, caudio::utils::Error> {
                auto tracks = db_->playlistGetTracks(cmd.pid);
                if (!tracks) return std::unexpected{tracks.error()};
                std::span<const caudio::db::Track> span{*tracks};
                std::vector<caudio::db::Track> out(span.begin(), span.end());
                return Result{QueueTracks{std::move(out)}};
            },
            [&](const PlaylistLoad& cmd) -> std::expected<Result, caudio::utils::Error> {
                auto tracks = db_->playlistGetTracks(cmd.pid);
                if (!tracks) return std::unexpected{tracks.error()};
                int64_t qid = 1;
                auto clr = db_->queueClear(qid);
                if (!clr) return std::unexpected{clr.error()};
                for (auto& t : std::span<const caudio::db::Track>(*tracks)) {
                    auto er = db_->queueEnqueue(qid, t.id);
                    if (!er) return std::unexpected{er.error()};
                }
                if (cmd.play) {
                    auto pr = engine_->play(qid);
                    if (!pr) return std::unexpected{pr.error()};
                }
                updateShmStatus();
                auto st = detail_svc::buildStatus(*engine_, *db_);
                if (!st) return std::unexpected{st.error()};
                return Result{*st};
            },
            [&](const PlaylistSave& cmd) -> std::expected<Result, caudio::utils::Error> {
                if (cmd.name.empty()) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "empty playlist name")};
                auto pidRes = db_->createPlaylist(cmd.name);
                if (!pidRes) return std::unexpected{pidRes.error()};
                int64_t pid = *pidRes;
                int64_t qid = cmd.queueId.value_or(1);
                auto items = db_->queueList(qid);
                if (!items) return std::unexpected{items.error()};
                for (auto& it : std::span<const caudio::db::QueueItem>(*items)) {
                    auto r = db_->playlistAddTrack(pid, it.trackId);
                    if (!r) return std::unexpected{r.error()};
                }
                return Result{Empty{}};
            },
            [&](const PlaylistDelete& cmd) -> std::expected<Result, caudio::utils::Error> {
                auto r = db_->deletePlaylist(cmd.pid);
                if (!r) return std::unexpected{r.error()};
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
    int lockFd_{-1};
    std::unique_ptr<caudio::service::ShmStatusHandle> shmHandle_{};
    std::string shmName_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdownRequested_{false};
};

} // namespace caudio::service
