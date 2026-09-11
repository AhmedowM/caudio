module;
// Internal helpers for caudio.service — not exported. This partition is imported
// by :impl but not re-exported by caudio.service, keeping internal linkage.

#include <algorithm>
#include <array>
#include <cctype>
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

module caudio.service:detail;

import caudio.utils;
import caudio.engine;
import caudio.db;
import caudio.cli;
import caudio.player;

namespace caudio::service::detail {

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

inline std::filesystem::path pidPathForSocket(const std::filesystem::path& dbPath,
                                              const std::string& /*socketPath*/) {
    auto r = caudio::cli::pidPathFor(dbPath);
    if (r) return *r;
    auto pp = dbPath.parent_path();
    if (pp.empty()) pp = std::filesystem::current_path();
    return pp / "caudio.pid";
}

inline std::filesystem::path lockPathForSocket(const std::filesystem::path& dbPath,
                                               const std::string& /*socketPath*/) {
    auto r = caudio::cli::lockPathFor(dbPath);
    if (r) return *r;
    auto pidPath = pidPathForSocket(dbPath, "");
    std::string dbStr = dbPath.generic_string();
    std::size_t hash = std::hash<std::string>{}(dbStr);
    return pidPath.parent_path() / ("caudio-" + std::to_string(hash) + ".lock");
}

inline std::string socketPathForDb(const std::filesystem::path& dbPath) {
    auto r = caudio::cli::socketPathFor(dbPath);
    if (r) return *r;
#ifdef _WIN32
    std::string dbStr = dbPath.generic_string();
    std::size_t hash = std::hash<std::string>{}(dbStr);
    return "\\\\.\\pipe\\caudio-" + std::to_string(hash);
#else
    auto pp = dbPath.parent_path();
    if (pp.empty()) pp = std::filesystem::current_path();
    std::error_code ec;
    std::filesystem::create_directories(pp, ec);
    std::string dbStr = dbPath.generic_string();
    std::size_t hash = std::hash<std::string>{}(dbStr);
    return (pp / ("caudio-" + std::to_string(hash) + ".sock")).generic_string();
#endif
}

inline bool probeSocketAlive(const std::string& sp) {
#ifdef _WIN32
    if (sp.empty()) return false;
    std::wstring w;
    w.reserve(sp.size());
    for (char c : sp) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    HANDLE h = ::CreateFileW(w.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        ::CloseHandle(h);
        return true;
    }
    DWORD err = ::GetLastError();
    if (err == 231 /*ERROR_PIPE_BUSY*/) {
        // Pipe exists but all instances busy - treat as alive
        return true;
    }
    if (err == 2 /*ERROR_FILE_NOT_FOUND*/ || err == 109 /*ERROR_BROKEN_PIPE*/) {
        return false;
    }
    // For other errors, try WaitNamedPipe to confirm pipe exists
    if (::WaitNamedPipeW(w.c_str(), 0)) {
        return true;
    }
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
#ifdef _WIN32
    std::error_code ec;
    auto parent = lockPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    std::wstring wpath;
    wpath.reserve(lockPath.native().size());
    for (wchar_t c : lockPath.native()) wpath.push_back(c);

    HANDLE h = ::CreateFileW(wpath.c_str(),
                             GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ,  // allow readers, deny writers
                             nullptr,
                             OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                             nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        outFd = -1;
        return false;
    }
    // Try to lock the first byte exclusively, non-blocking
    OVERLAPPED ov{};
    ov.Offset = 0;
    ov.OffsetHigh = 0;
    if (!::LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
        ::CloseHandle(h);
        outFd = -1;
        return false;  // ERROR_LOCK_VIOLATION (33) or other
    }
    outFd = reinterpret_cast<intptr_t>(h);
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
#ifdef _WIN32
    HANDLE h = reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd));
    OVERLAPPED ov{};
    ov.Offset = 0;
    ov.OffsetHigh = 0;
    ::UnlockFileEx(h, 0, 1, 0, &ov);
    ::CloseHandle(h);
#else
    ::flock(fd, LOCK_UN);
    ::close(fd);
#endif
}

inline bool checkPidAlive(int pid) {
    if (pid <= 0) return false;
#ifndef _WIN32
    return ::kill(pid, 0) == 0;
#else
    HANDLE h = ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    DWORD wait = ::WaitForSingleObject(h, 0);
    ::CloseHandle(h);
    return wait == WAIT_TIMEOUT;
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
    s.track_id = eng.currentTrackId();
    if (s.track_id != 0) {
        auto tr = db.getTrack(s.track_id);
        if (tr) {
            s.title = tr->title;
            s.artist = tr->artist;
            s.path = tr->path;
        }
    }
    try {
        auto items = db.queueList(1);
        if (items) {
            s.q_size = items->size();
            s.q_idx = 0;
            if (s.track_id != 0 && !items->empty()) {
                for (std::size_t i = 0; i < items->size(); ++i) {
                    if ((*items)[i].track_id == s.track_id) {
                        s.q_idx = i;
                        break;
                    }
                }
            }
        }
    } catch (...) {}
    return s;
}

inline std::filesystem::path resolveConfigPath(const std::filesystem::path& configPath,
                                               const std::filesystem::path& dbPath) {
    if (!configPath.empty()) return configPath;
    if (!dbPath.empty()) {
        auto pp = dbPath.parent_path();
        if (!pp.empty()) return pp / "config.json";
    }
    std::error_code ec;
    auto tmp = std::filesystem::temp_directory_path(ec);
    if (ec) tmp = std::filesystem::path("/tmp");
    return tmp / "caudio" / "config.json";
}

inline std::expected<std::string, caudio::utils::Error>
readConfigValueRaw(const std::filesystem::path& p, std::string_view key) {
    return caudio::cli::configGetRaw(p, key);
}

inline caudio::utils::Expected<void>
writeConfigValueRaw(const std::filesystem::path& p, std::string_view key, std::string_view value) {
    return caudio::cli::configSetRaw(p, key, value);
}

inline std::expected<std::vector<caudio::cli::ConfigValue>, caudio::utils::Error>
listConfigValuesRaw(const std::filesystem::path& p) {
    auto r = caudio::cli::configListRaw(p);
    if (!r) return std::unexpected{r.error()};
    std::vector<caudio::cli::ConfigValue> out;
    out.reserve(r->size());
    for (auto& kv : *r) out.push_back(caudio::cli::ConfigValue{kv.key, kv.value});
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
    if (ec) return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, ec.message())};
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Io, "cannot open file")};
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
        return static_cast<double>(frames) / static_cast<double>(sr);
    } catch (...) {
        return 0.0;
    }
}

} // namespace caudio::service::detail



