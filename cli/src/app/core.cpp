#ifdef _WIN32
// NOTE: <windows.h> must precede all other includes in this TU. thread.hpp
// hand-declares HANDLE/HMODULE/etc. when windows.h is absent, which then
// conflicts with the real declarations pulled in by CLI11. Keep this block
// first: clang-format must not sort it.
// clang-format off
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// clang-format on
#endif

#include "core.hpp"

#include <CLI/CLI.hpp>
#include <memory>

#include "parse.hpp"

#ifdef _WIN32
#if !defined(_WINDOWS_) && !defined(_WINDEF_) && !defined(_MINWINDEF_)
// Avoid including <windows.h> -- causes HMODULE conflict with caudio::utils (like
// service_paths.cpp) thread.hpp already defines HANDLE, DWORD, HMODULE, LPWSTR, etc. Provide
// missing decls.
using BOOL = int;
using LPCWSTR = const wchar_t*;
using LPSECURITY_ATTRIBUTES = void*;
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#ifndef DETACHED_PROCESS
#define DETACHED_PROCESS 0x00000008
#endif
#ifndef FALSE
#define FALSE 0
#endif
struct STARTUPINFOW {
    DWORD cb{0};
    LPCWSTR lpReserved{nullptr};
    LPCWSTR lpDesktop{nullptr};
    LPCWSTR lpTitle{nullptr};
    DWORD dwX{0};
    DWORD dwY{0};
    DWORD dwXSize{0};
    DWORD dwYSize{0};
    DWORD dwXCountChars{0};
    DWORD dwYCountChars{0};
    DWORD dwFillAttribute{0};
    DWORD dwFlags{0};
    short wShowWindow{0};
    short cbReserved2{0};
    void* lpReserved2{nullptr};
    HANDLE hStdInput{nullptr};
    HANDLE hStdOutput{nullptr};
    HANDLE hStdError{nullptr};
};
struct PROCESS_INFORMATION {
    HANDLE hProcess{nullptr};
    HANDLE hThread{nullptr};
    DWORD dwProcessId{0};
    DWORD dwThreadId{0};
};
extern "C" {
__declspec(dllimport) DWORD __stdcall GetModuleFileNameW(HANDLE, LPWSTR, DWORD);
__declspec(dllimport) DWORD __stdcall GetLastError();
__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE);
__declspec(dllimport) BOOL __stdcall CreateProcessW(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES,
                                                    LPSECURITY_ATTRIBUTES, BOOL, DWORD, void*,
                                                    LPCWSTR, STARTUPINFOW*, PROCESS_INFORMATION*);
}
#else
#include <windows.h>
#endif
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif
#include <caudio/client.hpp>
#include <caudio/client/ipc_client.hpp>
#include <caudio/client/output_formatter.hpp>
#include <caudio/config.hpp>
#include <caudio/db.hpp>
#include <caudio/engine.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/protocol.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/player.hpp>
#include <caudio/service.hpp>
#include <caudio/utils.hpp>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <caudio/utils/print.hpp>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace caudio::app {

namespace detail {
// Single source: delegate to caudio::app::parse::parseTime (parse.hpp) and adapt error type.
// parse.hpp returns expected<double,string>; app layer wraps string into utils::Error.
std::expected<double, caudio::utils::Error> parseTime(std::string_view s) {
    auto r = caudio::app::parse::parseTime(s);
    if (!r)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, r.error())};
    return *r;
}
// Single source: delegate to caudio::app::parse::parseSeek and adapt error type.
std::expected<double, caudio::utils::Error> parseSeek(std::string_view s) {
    auto r = caudio::app::parse::parseSeek(s);
    if (!r)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, r.error())};
    return *r;
}
// Single source: delegate to caudio::app::parse::parseVolume and adapt error type.
std::expected<caudio::ipc::VolumeSet, caudio::utils::Error> parseVolume(std::string_view s) {
    auto r = caudio::app::parse::parseVolume(s);
    if (!r)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, r.error())};
    caudio::app::parse::ParsedVolume pv = *r;
    caudio::ipc::VolumeSet vs{};
    vs.level = pv.level;
    vs.mute = pv.mute;
    vs.deltaPct = pv.deltaPct;
    return vs;
}
std::chrono::duration<double> parseDuration(std::string_view s) {
    auto t = parseTime(s);
    if (!t)
        return std::chrono::duration<double>{0};
    return std::chrono::duration<double>{*t};
}

void writePlaylistText(std::ostream& os, const std::vector<caudio::db::Track>& tracks,
                       std::string_view format) {
    if (format == "m3u") {
        os << "#EXTM3U\n";
        for (const auto& t : tracks) {
            int dur = static_cast<int>(std::round(t.duration));
            std::string title = t.title.empty() ? t.path : t.title;
            std::string artist = t.artist.empty() ? "" : t.artist;
            os << "#EXTINF:" << dur;
            if (!artist.empty())
                os << "," << artist << " - " << title;
            else
                os << "," << title;
            os << "\n";
            os << t.path << "\n";
        }
    } else if (format == "pls") {
        os << "[playlist]\n";
        int i = 1;
        for (const auto& t : tracks) {
            os << "File" << i << "=" << t.path << "\n";
            std::string title = t.title.empty() ? t.path : t.title;
            if (!t.artist.empty())
                title = t.artist + " - " + title;
            os << "Title" << i << "=" << title << "\n";
            int dur = static_cast<int>(std::round(t.duration));
            os << "Length" << i << "=" << dur << "\n";
            ++i;
        }
        os << "NumberOfEntries=" << tracks.size() << "\n";
        os << "Version=2\n";
    }
}

void writePlaylistJson(std::ostream& os, const std::vector<caudio::db::Track>& tracks) {
    nlohmann::ordered_json j;
    j["format"] = "caudio-playlist";
    j["version"] = 1;
    j["tracks"] = nlohmann::ordered_json::array();
    for (const auto& t : tracks) {
        j["tracks"].push_back(caudio::ipc::detail::trackToJson(t));
    }
    os << j.dump(2) << "\n";
}

} // namespace detail
using detail::parseSeek;
using detail::parseTime;
using detail::parseVolume;

App::App(caudio::config::Config cfg)
    : config_(std::move(cfg)), cli_(std::make_unique<CLI::App>("caudio - terminal player")) {
    cli_->set_version_flag("--version", std::string(caudio::versionFull));
}

App::~App() = default;

std::filesystem::path App::pidPathForConfig() const {
    // Canonical pid path -- single source via caudio.config (hash of dbPath + XDG/LOCALAPPDATA)
    auto r = caudio::config::pidPathFor(config_.dbPath);
    if (r)
        return *r;
    // fallback legacy
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

int App::handleStart(bool foreground) {
    auto conn = caudio::client::IpcClient::connect(config_.dbPath, config_.socketPath);
    if (conn) {
        caudio::println("daemon already running at {}", config_.socketPath);
        return 0;
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
            caudio::println(std::cerr, "start failed: {} {} (dbPath={})",
                         std::to_string(std::to_underlying(svc.error().code)), svc.error().message,
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
            caudio::println(std::cerr, "start failed: CreateProcess failed {} at {} db={}", err,
                         config_.socketPath, config_.dbPath.generic_string());
            return 1;
        }
        // Poll for pipe readiness: 1500ms total, 100ms interval x--15
        for (int i = 0; i < 15; ++i) {
            auto conn2 = caudio::client::IpcClient::connect(config_.dbPath, config_.socketPath);
            if (conn2) {
                caudio::println("daemon started at {}", config_.socketPath);
                return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        caudio::println(std::cerr, "daemon start failed, socket not reachable at {} db={}",
                     config_.socketPath, config_.dbPath.generic_string());
        return 1;
    }
}

int App::handleShutdown() {
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

int App::handlePreview(const std::string& file) {
    std::filesystem::path p{file};
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        caudio::println(std::cerr, "preview: file not found {}", file);
        return 1;
    }
    auto playerRes = caudio::player::Player::create();
    if (!playerRes) {
        caudio::println(std::cerr, "preview: player create failed {} {}",
                     std::to_string(std::to_underlying(playerRes.error().code)),
                     playerRes.error().message);
        return 1;
    }
    auto& player = *playerRes.value();
    auto openRes = player.open(file);
    if (!openRes) {
        caudio::println(std::cerr, "preview: open failed {} {}",
                     std::to_string(std::to_underlying(openRes.error().code)),
                     openRes.error().message);
        return 1;
    }
    auto playRes = player.play();
    if (!playRes) {
        caudio::println(std::cerr, "preview: play failed {} {}",
                     std::to_string(std::to_underlying(playRes.error().code)),
                     playRes.error().message);
        return 1;
    }
    caudio::println("preview playing {}", file);
    while (player.state() == caudio::player::State::Playing) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    caudio::println("preview done");
    return 0;
}
int App::run(int argc, char** argv) {
    if (argc > 0 && argv && argv[0])
        argv0_ = argv[0];
    std::string configPathStr;
    std::string logLevelStr;
    std::string deviceStr;
    cli_->add_option("--config", configPathStr, "Config file");
    cli_->add_option("--log-level", logLevelStr, "trace|debug|info|warn|error");
    cli_->add_option("--device", deviceStr, "Audio output device");
    bool daemonFlag = false;
    cli_->add_flag("--daemon", daemonFlag, "Internal daemon flag")->group("");
    bool globalFg = false;
    cli_->add_flag("--foreground", globalFg, "")->group("");
    bool fg = false;
    auto* startCmd = cli_->add_subcommand("start", "Start daemon");
    startCmd->add_flag("--foreground", fg, "Run in foreground");
    auto* shutdownCmd = cli_->add_subcommand("shutdown", "Stop daemon");
    auto* playCmd = cli_->add_subcommand("play", "Play current queue");
    auto* pauseCmd = cli_->add_subcommand("pause", "Pause playback");
    auto* resumeCmd = cli_->add_subcommand("resume", "Resume playback");
    auto* restartCmd = cli_->add_subcommand("restart", "Restart current track");
    auto* stopCmd = cli_->add_subcommand("stop", "Stop playback");
    auto* nextCmd = cli_->add_subcommand("next", "Next track");
    auto* prevCmd = cli_->add_subcommand("prev", "Prev track");
    std::string seekStr;
    auto* seekCmd = cli_->add_subcommand("seek", "Seek to position");
    seekCmd->add_option("time", seekStr, "mm:ss or seconds or +N/-N")->required();
    bool jsonFlag = false;
    bool statusWatch = false;
    int statusInterval = 1000;
    auto* statusCmd = cli_->add_subcommand("status", "Show status");
    statusCmd->add_flag("--json", jsonFlag, "JSON output");
    statusCmd->add_flag("--watch", statusWatch, "Continuous polling");
    statusCmd->add_flag("--follow", statusWatch, "Alias for --watch");
    statusCmd->add_option("--interval", statusInterval, "Poll interval in ms");
    std::string volumeArg;
    auto* volumeCmd = cli_->add_subcommand("volume", "Get/set volume");
    volumeCmd->add_option("level", volumeArg, "0-100|+N|-N|mute|unmute");
    auto* queueCmd = cli_->add_subcommand("queue", "Queue operations");
    bool qJson = false;
    auto* qList = queueCmd->add_subcommand("list", "List queue tracks");
    qList->add_flag("--json", qJson, "JSON output");
    auto* qQueues = queueCmd->add_subcommand("queues", "List all queues");
    std::int64_t qSwitchId = 0;
    auto* qSwitch = queueCmd->add_subcommand("switch", "Switch active queue");
    qSwitch->add_option("qid", qSwitchId, "Queue id")->required();
    std::string qAddQuery;
    bool qAddSearch = false;
    auto* qAdd = queueCmd->add_subcommand("add", "Add to queue");
    qAdd->add_option("query", qAddQuery, "id|path|query")->required();
    qAdd->add_flag("--search", qAddSearch, "Force FTS search")->group("");
    std::string qRemoveId;
    auto* qRemove = queueCmd->add_subcommand("remove", "Remove from queue");
    qRemove->add_option("id", qRemoveId, "index or id")->required();
    std::size_t qFrom = 0, qTo = 0;
    auto* qMove = queueCmd->add_subcommand("move", "Move within queue");
    qMove->add_option("from", qFrom, "from index")->required();
    qMove->add_option("to", qTo, "to index")->required();
    auto* qClear = queueCmd->add_subcommand("clear", "Clear queue");
    std::string qShuffleArg;
    auto* qShuffle = queueCmd->add_subcommand("shuffle", "Set shuffle");
    qShuffle->add_option("mode", qShuffleArg, "on|off");
    std::string qRepeatArg;
    auto* qRepeat = queueCmd->add_subcommand("repeat", "Set repeat");
    qRepeat->add_option("mode", qRepeatArg, "off|one|all");
    auto* plCmd = cli_->add_subcommand("playlist", "Playlist operations");
    bool plJson = false;
    auto* plList = plCmd->add_subcommand("list", "List playlists");
    plList->add_flag("--json", plJson, "JSON output");
    std::int64_t plTracksPid = 0;
    auto* plTracks = plCmd->add_subcommand("tracks", "Tracks in playlist");
    plTracks->add_option("pid", plTracksPid, "Playlist id")->required();
    std::int64_t plLoadPid = 0;
    bool plLoadPlay = false;
    auto* plLoad = plCmd->add_subcommand("load", "Load playlist into queue");
    plLoad->add_option("pid", plLoadPid, "Playlist id")->required();
    plLoad->add_flag("--play", plLoadPlay, "Play after load");
    std::string plSaveName;
    std::int64_t plSaveQid = 0;
    auto* plSave = plCmd->add_subcommand("save", "Save queue as playlist");
    plSave->add_option("name", plSaveName, "Playlist name")->required();
    plSave->add_option("--queue", plSaveQid, "Queue id");
    std::int64_t plDeletePid = 0;
    auto* plDelete = plCmd->add_subcommand("delete", "Delete playlist");
    plDelete->add_option("pid", plDeletePid, "Playlist id")->required();
    std::int64_t plRenamePid = 0;
    std::string plRenameName;
    auto* plRename = plCmd->add_subcommand("rename", "Rename playlist");
    plRename->add_option("pid", plRenamePid, "Playlist id")->required();
    plRename->add_option("name", plRenameName, "New name")->required();
    std::int64_t plExportPid = 0;
    std::string plExportPath;
    std::string plExportFormat = "m3u";
    auto* plExport = plCmd->add_subcommand("export", "Export playlist to file");
    plExport->add_option("pid", plExportPid, "Playlist id")->required();
    plExport->add_option("path", plExportPath, "Output file path")->required();
    plExport->add_option("--format", plExportFormat, "Format: m3u|pls|json")
        ->check(CLI::IsMember({"m3u", "pls", "json"}));
    std::string plImportPath;
    std::string plImportName;
    auto* plImport = plCmd->add_subcommand("import", "Import playlist from file");
    plImport->add_option("path", plImportPath, "Input file path")->required();
    plImport->add_option("--name", plImportName, "Playlist name (default: filename)");
    auto* libCmd = cli_->add_subcommand("library", "Library operations");
    std::string libScanPath;
    std::string libScanMode = "sampled";
    auto* libScan = libCmd->add_subcommand("scan", "Scan library");
    libScan->add_option("--path", libScanPath, "Scan path");
    libScan->add_option("--mode", libScanMode, "sampled|full");
    std::string libSearchQuery;
    int libSearchLimit = 50;
    bool libSearchJson = false;
    auto* libSearch = libCmd->add_subcommand("search", "Search library");
    libSearch->add_option("query", libSearchQuery, "FTS query")->required();
    libSearch->add_option("--limit", libSearchLimit, "Limit");
    libSearch->add_flag("--json", libSearchJson, "JSON output");
    bool libStatsJson = false;
    bool libStatsDetailed = false;
    auto* libStats = libCmd->add_subcommand("stats", "Library stats");
    libStats->add_flag("--json", libStatsJson, "JSON output");
    libStats->add_flag("--detailed", libStatsDetailed,
                       "Show detailed stats (most played, total play time)");
    std::string libAddPath;
    bool libAddRecursive = false;
    auto* libAdd = libCmd->add_subcommand("add", "Add file or directory to library");
    libAdd->add_option("path", libAddPath, "File or directory path")->required();
    libAdd->add_flag("--recursive", libAddRecursive, "Recurse into subdirectories");
    std::string libRemoveQuery;
    auto* libRemove = libCmd->add_subcommand("remove", "Remove track from library");
    libRemove->add_option("id", libRemoveQuery, "Track id or path")->required();
    int libListLimit = 50;
    int libListOffset = 0;
    std::string libListQuery;
    std::string libListArtist;
    std::string libListAlbum;
    std::string libListGenre;
    bool libListJson = false;
    auto* libList = libCmd->add_subcommand("list", "List all tracks in library");
    libList->add_option("--query", libListQuery, "Search query (title, artist, album, genre)");
    libList->add_option("--artist", libListArtist, "Filter by artist");
    libList->add_option("--album", libListAlbum, "Filter by album");
    libList->add_option("--genre", libListGenre, "Filter by genre");
    libList->add_option("--limit", libListLimit, "Limit results");
    libList->add_option("--offset", libListOffset, "Offset for pagination");
    libList->add_flag("--json", libListJson, "JSON output");
    auto* tagCmd = cli_->add_subcommand("tag", "Tag operations");
    std::int64_t tagEditId = 0;
    std::string tagEditField;
    std::string tagEditValue;
    auto* tagEdit = tagCmd->add_subcommand("edit", "Edit track tag");
    tagEdit->add_option("id", tagEditId, "Track id")->required();
    tagEdit
        ->add_option("field", tagEditField,
                     "Field (title,artist,album,album_artist,genre,year,track_number,disc_number)")
        ->required()
        ->check(CLI::IsMember({"title", "artist", "album", "album_artist", "genre", "year",
                               "track_number", "disc_number"}));
    tagEdit->add_option("value", tagEditValue, "New value")->required();
    std::int64_t tagGetId = 0;
    bool tagGetJson = false;
    auto* tagGet = tagCmd->add_subcommand("get", "Get track tags");
    tagGet->add_option("id", tagGetId, "Track id")->required();
    tagGet->add_flag("--json", tagGetJson, "JSON output");
    auto* historyCmd = cli_->add_subcommand("history", "Playback history operations");
    bool historyJson = false;
    int historyLimit = 50;
    auto* historyList = historyCmd->add_subcommand("list", "List playback history");
    historyList->add_flag("--json", historyJson, "JSON output");
    historyList->add_option("--limit", historyLimit, "Limit entries");
    auto* historyClear = historyCmd->add_subcommand("clear", "Clear playback history");
    std::string previewFile;
    auto* previewCmd = cli_->add_subcommand("preview", "Preview file (ephemeral)");
    previewCmd->add_option("file", previewFile, "File path")->required();
    auto* cfgCmd = cli_->add_subcommand("config", "Config operations");
    std::string cfgGetKey;
    auto* cfgGet = cfgCmd->add_subcommand("get", "Get config value");
    cfgGet->add_option("key", cfgGetKey, "Key")->required();
    std::string cfgSetKey, cfgSetVal;
    auto* cfgSet = cfgCmd->add_subcommand("set", "Set config value");
    cfgSet->add_option("key", cfgSetKey, "Key")->required();
    cfgSet->add_option("value", cfgSetVal, "Value")->required();
    bool cfgListJson = false;
    auto* cfgList = cfgCmd->add_subcommand("list", "List config");
    cfgList->add_flag("--json", cfgListJson, "JSON output");
    std::string cfgExportPath;
    auto* cfgExport = cfgCmd->add_subcommand("export", "Export config");
    cfgExport->add_option("path", cfgExportPath, "Path")->required();
    std::string cfgImportPath;
    auto* cfgImport = cfgCmd->add_subcommand("import", "Import config");
    cfgImport->add_option("path", cfgImportPath, "Path")->required();
    std::string cfgResetKey;
    auto* cfgReset = cfgCmd->add_subcommand("reset", "Reset config to defaults");
    cfgReset->add_option("key", cfgResetKey, "Key to reset (omit to reset all)");
    // Device commands
    auto* deviceCmd = cli_->add_subcommand("device", "Audio device operations");
    bool devJson = false;
    auto* devList = deviceCmd->add_subcommand("list", "List audio output devices");
    devList->add_flag("--json", devJson, "JSON output");
    std::string devSetId;
    auto* devSet = deviceCmd->add_subcommand("set", "Set default audio device");
    devSet->add_option("id", devSetId, "Device ID")->required();
    std::string devTestId;
    auto* devTest = deviceCmd->add_subcommand("test", "Test audio device (play tone)");
    devTest->add_option("--id", devTestId, "Device ID (default: current config)");
    bool infoJson = false;
    auto* infoCmd = cli_->add_subcommand("info", "Show current track info");
    infoCmd->add_flag("--json", infoJson, "JSON output");
    try {
        cli_->parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return cli_->exit(e);
    }
    if (!configPathStr.empty())
        config_.configPath = std::filesystem::path(configPathStr);
    {
        auto loaded = caudio::config::loadConfig(config_.configPath);
        if (loaded) {
            config_ = *loaded;
            if (!configPathStr.empty())
                config_.configPath = std::filesystem::path(configPathStr);
            // If config file didn't exist, persist defaults for next run
            std::error_code ec2;
            if (!std::filesystem::exists(config_.configPath, ec2)) {
                std::error_code ec3;
                auto parent = config_.configPath.parent_path();
                if (!parent.empty())
                    std::filesystem::create_directories(parent, ec3);
                (void)caudio::config::saveConfig(config_);
            }
        } else {
            // loadConfig failed (e.g. corrupt); keep current config_ which has robust defaults
            // ensure directories exist for db and config
            std::error_code ec2;
            if (!config_.dbPath.empty()) {
                auto parent = config_.dbPath.parent_path();
                if (!parent.empty())
                    std::filesystem::create_directories(parent, ec2);
            }
        }
    }
    if (!deviceStr.empty())
        config_.device = deviceStr;
    if (!logLevelStr.empty()) {
        if (logLevelStr == "trace")
            config_.logLevel = 0;
        else if (logLevelStr == "debug")
            config_.logLevel = 1;
        else if (logLevelStr == "info")
            config_.logLevel = 2;
        else if (logLevelStr == "warn")
            config_.logLevel = 3;
        else if (logLevelStr == "error")
            config_.logLevel = 4;
    }
    if (config_.socketPath.empty() && !config_.dbPath.empty()) {
        auto sp = caudio::config::socketPathFor(config_.dbPath);
        if (sp)
            config_.socketPath = *sp;
    }
    // Ensure db parent dir exists before any operation (fixes "unable to open database file")
    {
        std::error_code ec2;
        auto parent = config_.dbPath.parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, ec2);
    }
    // Internal daemon mode: if --daemon present, run Service foreground immediately (child process)
    if (daemonFlag) {
        return handleStart(true);
    }
    auto sendViaClient = [&](const caudio::ipc::Command& cmd, bool asJson) -> int {
        caudio::client::Client client{config_.dbPath, config_.socketPath};
        auto timeout = std::chrono::milliseconds{2000};
        auto res = client.send(cmd, timeout);
        caudio::client::OutputFormatter fmt{asJson};
        if (!res) {
            caudio::ipc::Result errRes{res.error()};
            fmt.print(errRes, std::cerr);
            return 1;
        }
        if (std::holds_alternative<caudio::utils::Error>(*res)) {
            fmt.print(*res, std::cerr);
            return 1;
        }
        fmt.print(*res, std::cout);
        return 0;
    };
    if (startCmd->parsed())
        return handleStart(fg || globalFg);
    if (shutdownCmd->parsed())
        return handleShutdown();
    if (playCmd->parsed()) {
        caudio::ipc::Command cmd{caudio::ipc::Play{}};
        return sendViaClient(cmd, false);
    }
    if (pauseCmd->parsed()) {
        caudio::ipc::Command cmd{caudio::ipc::Pause{}};
        return sendViaClient(cmd, false);
    }
    if (resumeCmd->parsed()) {
        caudio::ipc::Command cmd{caudio::ipc::Resume{}};
        return sendViaClient(cmd, false);
    }
    if (restartCmd->parsed()) {
        caudio::ipc::Command cmd{caudio::ipc::Restart{}};
        return sendViaClient(cmd, false);
    }
    if (stopCmd->parsed()) {
        caudio::ipc::Command cmd{caudio::ipc::Stop{}};
        return sendViaClient(cmd, false);
    }
    if (nextCmd->parsed()) {
        caudio::ipc::Command cmd{caudio::ipc::Next{}};
        return sendViaClient(cmd, false);
    }
    if (prevCmd->parsed()) {
        caudio::ipc::Command cmd{caudio::ipc::Prev{}};
        return sendViaClient(cmd, false);
    }
    if (seekCmd->parsed()) {
        auto parsed = detail::parseSeek(seekStr);
        if (!parsed) {
            caudio::println(std::cerr, "seek: {}", parsed.error().message);
            return 1;
        }
        double target = *parsed;
        bool isRelative = !seekStr.empty() && (seekStr.front() == '+' || seekStr.front() == '-');
        if (isRelative) {
            caudio::client::Client client{config_.dbPath, config_.socketPath};
            auto sres = client.send(caudio::ipc::Command{caudio::ipc::StatusReq{}});
            double pos = 0;
            bool hasPos = false;
            if (sres) {
                if (auto* ps = std::get_if<caudio::ipc::Status>(&*sres)) {
                    pos = ps->pos;
                    hasPos = true;
                }
            }
            if (hasPos) {
                target = pos + target;
                if (target < 0)
                    target = 0;
            } else {
                if (target < 0)
                    target = 0;
            }
        }
        caudio::ipc::Command cmd{caudio::ipc::Seek{target}};
        return sendViaClient(cmd, false);
    }
    if (statusCmd->parsed()) {
        if (statusWatch) {
            if (statusInterval <= 0) {
                caudio::println(std::cerr, "status --interval must be positive");
                return 1;
            }
            using namespace std::chrono;
            caudio::client::OutputFormatter fmt{jsonFlag};
            while (true) {
                caudio::client::Client client{config_.dbPath, config_.socketPath};
                auto res =
                    client.send(caudio::ipc::Command{caudio::ipc::StatusReq{}}, milliseconds{2000});
                if (!res) {
                    caudio::ipc::Result errRes{res.error()};
                    if (jsonFlag) {
                        caudio::println(std::cerr, "{}", caudio::ipc::toJson(errRes).dump());
                    } else {
                        fmt.print(errRes, std::cerr);
                    }
                    return 1;
                }
                if (!jsonFlag) {
                    auto now = system_clock::now();
                    std::time_t t = system_clock::to_time_t(now);
                    std::tm tm{};
#ifdef _WIN32
                    localtime_s(&tm, &t);
#else
                    localtime_r(&t, &tm);
#endif
                    char buf[32];
                    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
                    caudio::println(std::cout, "[{}]", buf);
                    fmt.print(*res, std::cout);
                } else {
                    // Compact JSON per line for streaming to avoid multi-line log spam
                    caudio::println(std::cout, "{}", caudio::ipc::toJson(*res).dump());
                }
                std::cout << std::flush;
                if (std::holds_alternative<caudio::utils::Error>(*res))
                    return 1;
                std::this_thread::sleep_for(milliseconds{statusInterval});
            }
        }
        caudio::ipc::Command cmd{caudio::ipc::StatusReq{}};
        return sendViaClient(cmd, jsonFlag);
    }
    if (volumeCmd->parsed()) {
        auto pv = detail::parseVolume(volumeArg);
        if (!pv) {
            caudio::println(std::cerr, "volume: {}", pv.error().message);
            return 1;
        }
        caudio::ipc::Command cmd{*pv};
        return sendViaClient(cmd, false);
    }
    if (queueCmd->parsed()) {
        if (qList->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::QueueList{}};
            return sendViaClient(cmd, qJson);
        }
        if (qQueues->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::QueueQueues{}};
            return sendViaClient(cmd, false);
        }
        if (qSwitch->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::QueueSwitch{qSwitchId}};
            return sendViaClient(cmd, false);
        }
        if (qAdd->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::QueueAdd{qAddQuery, qAddSearch}};
            return sendViaClient(cmd, false);
        }
        if (qRemove->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::QueueRemove{qRemoveId}};
            return sendViaClient(cmd, false);
        }
        if (qMove->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::QueueMove{qFrom, qTo}};
            return sendViaClient(cmd, false);
        }
        if (qClear->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::QueueClear{}};
            return sendViaClient(cmd, false);
        }
        if (qShuffle->parsed()) {
            std::optional<bool> on;
            if (qShuffleArg == "on")
                on = true;
            else if (qShuffleArg == "off")
                on = false;
            else if (!qShuffleArg.empty()) {
                caudio::println(std::cerr, "shuffle: invalid mode {}", qShuffleArg);
                return 1;
            }
            caudio::ipc::Command cmd{caudio::ipc::QueueShuffle{on}};
            return sendViaClient(cmd, false);
        }
        if (qRepeat->parsed()) {
            std::optional<caudio::engine::RepeatMode> m;
            if (qRepeatArg == "off")
                m = caudio::engine::RepeatMode::Off;
            else if (qRepeatArg == "one")
                m = caudio::engine::RepeatMode::One;
            else if (qRepeatArg == "all")
                m = caudio::engine::RepeatMode::All;
            else if (!qRepeatArg.empty()) {
                caudio::println(std::cerr, "repeat: invalid mode {}", qRepeatArg);
                return 1;
            }
            caudio::ipc::Command cmd{caudio::ipc::QueueRepeat{m}};
            return sendViaClient(cmd, false);
        }
        std::cout << queueCmd->help() << "\n";
        return 0;
    }
    if (plCmd->parsed()) {
        if (plList->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::PlaylistList{}};
            return sendViaClient(cmd, plJson);
        }
        if (plTracks->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::PlaylistTracks{plTracksPid}};
            return sendViaClient(cmd, false);
        }
        if (plLoad->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::PlaylistLoad{plLoadPid, plLoadPlay}};
            return sendViaClient(cmd, false);
        }
        if (plSave->parsed()) {
            std::optional<std::int64_t> qid;
            if (plSave->get_option("--queue")->count() > 0)
                qid = plSaveQid;
            caudio::ipc::Command cmd{caudio::ipc::PlaylistSave{plSaveName, qid}};
            return sendViaClient(cmd, false);
        }
        if (plDelete->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::PlaylistDelete{plDeletePid}};
            return sendViaClient(cmd, false);
        }
        if (plRename->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::PlaylistRename{plRenamePid, plRenameName}};
            return sendViaClient(cmd, false);
        }
        if (plExport->parsed()) {
            caudio::ipc::Command cmd{
                caudio::ipc::PlaylistExport{plExportPid, plExportPath, plExportFormat}};
            caudio::client::Client client{config_.dbPath, config_.socketPath};
            auto timeout = std::chrono::milliseconds{5000};
            auto cliRes = client.send(cmd, timeout);
            if (!cliRes) {
                caudio::println(std::cerr, "export: {}", cliRes.error().message);
                return 1;
            }
            if (std::holds_alternative<caudio::utils::Error>(*cliRes)) {
                caudio::println(std::cerr, "export: {}",
                             std::get<caudio::utils::Error>(*cliRes).message);
                return 1;
            }
            if (std::holds_alternative<caudio::ipc::PlaylistData>(*cliRes)) {
                auto& pd = std::get<caudio::ipc::PlaylistData>(*cliRes);
                std::ofstream ofs(plExportPath);
                if (!ofs) {
                    caudio::println(std::cerr, "export: failed to open output file");
                    return 1;
                }
                if (pd.format == "m3u" || pd.format == "pls") {
                    detail::writePlaylistText(ofs, pd.tracks, pd.format);
                } else if (pd.format == "json") {
                    detail::writePlaylistJson(ofs, pd.tracks);
                }
                ofs.close();
                caudio::println("exported {} tracks to {}", pd.tracks.size(), plExportPath);
                return 0;
            }
            caudio::println(std::cerr, "export: unexpected response");
            return 1;
        }
        if (plImport->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::PlaylistImport{
                plImportPath, plImportName.empty() ? std::optional<std::string>{}
                                                   : std::optional<std::string>{plImportName}}};
            return sendViaClient(cmd, false);
        }
        std::cout << plCmd->help() << "\n";
        return 0;
    }
    if (libCmd->parsed()) {
        if (libScan->parsed()) {
            std::optional<std::string> p;
            if (!libScanPath.empty())
                p = libScanPath;
            caudio::ipc::Command cmd{caudio::ipc::LibraryScan{p, libScanMode}};
            return sendViaClient(cmd, false);
        }
        if (libSearch->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::LibrarySearch{libSearchQuery, libSearchLimit}};
            return sendViaClient(cmd, libSearchJson);
        }
        if (libStats->parsed()) {
            if (libStatsDetailed) {
                caudio::ipc::Command cmd{caudio::ipc::LibraryStatsDetailed{}};
                return sendViaClient(cmd, libStatsJson);
            }
            caudio::ipc::Command cmd{caudio::ipc::LibraryStats{}};
            return sendViaClient(cmd, libStatsJson);
        }
        if (libList->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::LibraryList{
                libListQuery.empty() ? std::optional<std::string>{}
                                     : std::optional<std::string>{libListQuery},
                libListLimit, libListOffset,
                libListArtist.empty() ? std::optional<std::string>{}
                                      : std::optional<std::string>{libListArtist},
                libListAlbum.empty() ? std::optional<std::string>{}
                                     : std::optional<std::string>{libListAlbum},
                libListGenre.empty() ? std::optional<std::string>{}
                                     : std::optional<std::string>{libListGenre}}};
            return sendViaClient(cmd, libListJson);
        }
        if (libAdd->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::LibraryAdd{libAddPath, libAddRecursive}};
            return sendViaClient(cmd, false);
        }
        if (libRemove->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::LibraryRemove{libRemoveQuery}};
            return sendViaClient(cmd, false);
        }
        std::cout << libCmd->help() << "\n";
        return 0;
    }
    if (tagCmd->parsed()) {
        if (tagEdit->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::TagEdit{tagEditId, tagEditField, tagEditValue}};
            return sendViaClient(cmd, false);
        }
        if (tagGet->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::TagGet{tagGetId}};
            return sendViaClient(cmd, tagGetJson);
        }
        std::cout << tagCmd->help() << "\n";
        return 0;
    }
    if (historyCmd->parsed()) {
        if (historyList->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::HistoryList{historyLimit}};
            return sendViaClient(cmd, historyJson);
        }
        if (historyClear->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::HistoryClear{}};
            return sendViaClient(cmd, false);
        }
        std::cout << historyCmd->help() << "\n";
        return 0;
    }
    if (previewCmd->parsed())
        return handlePreview(previewFile);
    if (cfgCmd->parsed()) {
        if (cfgGet->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::ConfigGet{cfgGetKey}};
            return sendViaClient(cmd, false);
        }
        if (cfgSet->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::ConfigSet{cfgSetKey, cfgSetVal}};
            return sendViaClient(cmd, false);
        }
        if (cfgList->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::ConfigList{}};
            return sendViaClient(cmd, cfgListJson);
        }
        if (cfgExport->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::ConfigExport{cfgExportPath}};
            return sendViaClient(cmd, false);
        }
        if (cfgImport->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::ConfigImport{cfgImportPath}};
            return sendViaClient(cmd, false);
        }
        if (cfgReset->parsed()) {
            std::optional<std::string> k;
            if (cfgReset->count("key") > 0 && !cfgResetKey.empty())
                k = cfgResetKey;
            else if (cfgReset->count("key") > 0 && cfgResetKey.empty()) {
                // explicit empty string passed -> treat as error
                caudio::println(std::cerr, "config reset: empty key");
                return 1;
            }
            caudio::ipc::Command cmd{caudio::ipc::ConfigReset{k}};
            return sendViaClient(cmd, false);
        }
        std::cout << cfgCmd->help() << "\n";
        return 0;
    }
    if (deviceCmd->parsed()) {
        if (devList->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::DeviceList{}};
            return sendViaClient(cmd, devJson);
        }
        if (devSet->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::DeviceSet{devSetId}};
            return sendViaClient(cmd, false);
        }
        if (devTest->parsed()) {
            std::optional<std::string> id;
            if (!devTestId.empty())
                id = devTestId;
            caudio::ipc::Command cmd{caudio::ipc::DeviceTest{id}};
            return sendViaClient(cmd, false);
        }
        std::cout << deviceCmd->help() << "\n";
        return 0;
    }
    if (infoCmd->parsed()) {
        caudio::ipc::Command cmd{caudio::ipc::Info{}};
        return sendViaClient(cmd, infoJson);
    }
    std::cout << cli_->help() << "\n";
    return 0;
}

} // namespace caudio::app
