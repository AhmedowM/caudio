#include <cstdio>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
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
// Granular API headers used directly: kept after windows.h.
// clang-format on
#include <errhandlingapi.h>
#include <handleapi.h>
#include <libloaderapi.h>
#include <minwindef.h>
#include <processthreadsapi.h>
#endif

#include <CLI/CLI.hpp>
#include <algorithm>
#include <caudio/client/client_core.hpp>
#include <caudio/db/db_types.hpp>
#include <caudio/db/json.hpp>
#include <caudio/db/scan.hpp>
#include <caudio/engine/engine_types.hpp>
#include <caudio/player/player_core.hpp>
#include <caudio/service/service_core.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/json.hpp>
#include <caudio/utils/result.hpp>
#include <caudio/version_config.hpp>
#include <memory>
#include <set>
#include <stop_token>
#include <system_error>

#include "shell.hpp"
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
#include <caudio/client/ipc_client.hpp>
#include <caudio/client/output_formatter.hpp>
#include <caudio/config.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/protocol.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/print.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace caudio::app::cli {

namespace detail {
// Single source: delegate to caudio::app::cli::parse::parseTime (parse.hpp) and adapt error type.
// parse.hpp returns expected<double,string>; app layer wraps string into utils::Error.
std::expected<double, caudio::utils::Error> parseTime(std::string_view s) {
    auto r = caudio::app::cli::parse::parseTime(s);
    if (!r)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, r.error())};
    return *r;
}
// Single source: delegate to caudio::app::cli::parse::parseSeek and adapt error type.
std::expected<double, caudio::utils::Error> parseSeek(std::string_view s) {
    auto r = caudio::app::cli::parse::parseSeek(s);
    if (!r)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, r.error())};
    return *r;
}
// Single source: delegate to caudio::app::cli::parse::parseVolume and adapt error type.
std::expected<caudio::ipc::VolumeSet, caudio::utils::Error> parseVolume(std::string_view s) {
    auto r = caudio::app::cli::parse::parseVolume(s);
    if (!r)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, r.error())};
    caudio::app::cli::parse::ParsedVolume pv = *r;
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
    caudio::utils::Json j;
    j["format"] = "caudio-playlist";
    j["version"] = 1;
    j["tracks"] = caudio::utils::Json::array();
    for (const auto& t : tracks) {
        j["tracks"].push_back(caudio::db::trackToJson(t));
    }
    os << j.dump(2) << "\n";
}

// Compact one-line status for `status --watch` (cli-local; the library
// formatter owns the full block). Keeps per-interval output to one line.
std::string fmtClock(double secs) {
    if (secs < 0)
        secs = 0;
    long total = static_cast<long>(secs);
    if (long h = total / 3600; h > 0)
        return std::format("{:02}:{:02}:{:02}", h, (total % 3600) / 60, total % 60);
    return std::format("{:02}:{:02}", (total % 3600) / 60, total % 60);
}

std::string statusLine(const caudio::ipc::Status& st) {
    std::string s{caudio::ipc::detail::playbackStateToString(st.state)};
    std::string who;
    if (!st.artist.empty() && !st.title.empty())
        who = st.artist + " - " + st.title;
    else
        who = st.artist + st.title;
    if (!who.empty())
        s += " " + who;
    s += " [" + fmtClock(st.pos) + "/" + fmtClock(st.dur) + "]";
    return s;
}

// Human track label for transport confirmations: no ids, no queue
// positions (see `info` / `queue list` for those).
std::string trackWho(const caudio::ipc::Status& st) {
    if (!st.artist.empty() && !st.title.empty())
        return st.artist + " - " + st.title;
    if (!st.artist.empty())
        return st.artist;
    if (!st.title.empty())
        return st.title;
    if (!st.path.empty()) {
        std::string fn = std::filesystem::path(st.path).filename().generic_string();
        if (!fn.empty())
            return fn;
    }
    return "unknown track";
}

// ANSI color only on interactive terminals honoring NO_COLOR.
bool useColor() {
    if (std::getenv("NO_COLOR") != nullptr)
        return false;
#ifdef _WIN32
    return ::_isatty(::_fileno(stdout)) != 0;
#else
    return ::isatty(STDOUT_FILENO) != 0;
#endif
}

// Human label for added tracks: "artist - title: file.ext", artist/title
// parts omitted when empty, bare filename when both are.
std::string addedLabel(const caudio::db::Track& t) {
    std::string fn = std::filesystem::path(t.path).filename().generic_string();
    std::string who;
    if (!t.artist.empty() && !t.title.empty())
        who = t.artist + " - " + t.title;
    else
        who = t.artist + t.title;
    if (!who.empty() && !fn.empty())
        return who + ": " + fn;
    if (!fn.empty())
        return fn;
    if (!who.empty())
        return who;
    return "track " + std::to_string(t.id);
}

// Case-insensitive wildcard match (* and ? only) for queue-add globs.
bool wildcardMatch(std::string_view pat, std::string_view name) {
    std::size_t px = 0, nx = 0, star = std::string_view::npos, ss = 0;
    auto lower = [](char c) {
        return (char)std::tolower((unsigned char)c);
    };
    while (nx < name.size()) {
        if (px < pat.size() && (pat[px] == '?' || lower(pat[px]) == lower(name[nx]))) {
            ++px;
            ++nx;
        } else if (px < pat.size() && pat[px] == '*') {
            star = px++;
            ss = nx;
        } else if (star != std::string_view::npos) {
            px = star + 1;
            nx = ++ss;
        } else {
            return false;
        }
    }
    while (px < pat.size() && pat[px] == '*')
        ++px;
    return px == pat.size();
}

bool hasGlobChars(std::string_view s) {
    return s.find_first_of("*?") != std::string_view::npos;
}

// Expand one queue-add token: glob (non-recursive filename match in the
// pattern's parent dir), folder (audio files, top-level unless recursive),
// or single file absolutized to the CLI working directory. Anything else is
// returned via unmatched for the caller to diagnose.
void expandAddToken(const std::string& token, bool recursive, std::vector<std::string>& files,
                    std::vector<std::string>& unmatched) {
    std::error_code ec;
    if (hasGlobChars(token)) {
        std::filesystem::path p(token);
        std::filesystem::path dir = p.parent_path();
        if (dir.empty())
            dir = ".";
        std::string pat = p.filename().generic_string();
        std::vector<std::string> hits;
        for (auto it = std::filesystem::directory_iterator(dir, ec);
             it != std::filesystem::directory_iterator(); ++it) {
            if (ec)
                break;
            std::error_code e2;
            if (!it->is_regular_file(e2) || e2)
                continue;
            std::string fn = it->path().filename().generic_string();
            if (wildcardMatch(pat, fn) && caudio::db::detail::hasAudioExt(it->path())) {
                auto abs = std::filesystem::absolute(it->path(), e2);
                hits.push_back(e2 ? it->path().generic_string() : abs.generic_string());
            }
        }
        if (hits.empty()) {
            unmatched.push_back(token);
            return;
        }
        std::sort(hits.begin(), hits.end());
        files.insert(files.end(), hits.begin(), hits.end());
        return;
    }
    std::filesystem::path p(token);
    if (std::filesystem::is_directory(p, ec) && !ec) {
        std::vector<std::string> hits;
        if (recursive) {
            for (auto it = std::filesystem::recursive_directory_iterator(
                      p, std::filesystem::directory_options::skip_permission_denied, ec);
                  it != std::filesystem::recursive_directory_iterator(); ++it) {
                if (ec)
                    break;
                std::error_code e2;
                if (it->is_regular_file(e2) && !e2 &&
                    caudio::db::detail::hasAudioExt(it->path())) {
                    auto abs = std::filesystem::absolute(it->path(), e2);
                    hits.push_back(e2 ? it->path().generic_string() : abs.generic_string());
                }
            }
        } else {
            for (auto it = std::filesystem::directory_iterator(p, ec);
                 it != std::filesystem::directory_iterator(); ++it) {
                if (ec)
                    break;
                std::error_code e2;
                if (it->is_regular_file(e2) && !e2 &&
                    caudio::db::detail::hasAudioExt(it->path())) {
                    auto abs = std::filesystem::absolute(it->path(), e2);
                    hits.push_back(e2 ? it->path().generic_string() : abs.generic_string());
                }
            }
        }
        if (hits.empty()) {
            unmatched.push_back(token);
            return;
        }
        std::sort(hits.begin(), hits.end());
        files.insert(files.end(), hits.begin(), hits.end());
        return;
    }
    if (std::filesystem::is_regular_file(p, ec) && !ec) {
        auto abs = std::filesystem::absolute(p, ec);
        files.push_back(ec ? p.generic_string() : abs.generic_string());
        return;
    }
    unmatched.push_back(token);
}

// Comparable path key: absolute + normalized (+ lowercase on Windows).
// Queued rows stored as relative paths resolve against the CLI working
// directory, which matches rows the CLI itself added.
std::string pathKey(const std::string& p) {
    std::error_code ec;
    auto abs = std::filesystem::absolute(p, ec);
    std::string s = ec ? p : abs.lexically_normal().generic_string();
#ifdef _WIN32
    for (auto& c : s)
        c = (char)std::tolower((unsigned char)c);
#endif
    return s;
}

} // namespace detail
using detail::parseSeek;
using detail::parseTime;
using detail::parseVolume;

Shell::Shell(caudio::config::Config cfg)
    : config_(cfg), app_(std::move(cfg)), cli_(std::make_unique<CLI::App>("caudio - terminal player")) {
    cli_->set_version_flag("--version", std::string(caudio::versionFull));
}

Shell::~Shell() = default;

int Shell::handlePreview(const std::string& file) {
    std::filesystem::path p{file};
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        caudio::println(std::cerr, "preview: file not found {}", file);
        return 1;
    }
    auto playerRes = caudio::player::Player::create();
    if (!playerRes) {
        caudio::println(std::cerr, "preview: player create failed ({}): {}",
                        caudio::utils::toString(playerRes.error().code), playerRes.error().message);
        return 1;
    }
    auto& player = *playerRes.value();
    auto openRes = player.open(file);
    if (!openRes) {
        caudio::println(std::cerr, "preview: open failed ({}): {}",
                        caudio::utils::toString(openRes.error().code), openRes.error().message);
        return 1;
    }
    auto playRes = player.play();
    if (!playRes) {
        caudio::println(std::cerr, "preview: play failed ({}): {}",
                        caudio::utils::toString(playRes.error().code), playRes.error().message);
        return 1;
    }
    caudio::println("preview playing {}", file);
    while (player.state() == caudio::player::State::Playing) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    caudio::println("preview done");
    return 0;
}
int Shell::run(int argc, char** argv) {
    if (argc > 0 && argv && argv[0])
        app_.setArgv0(argv[0]);
    std::string configPathStr;
    std::string logLevelStr;
    std::string deviceStr;
    cli_->add_option("--config", configPathStr, "Config file");
    cli_->add_option("--log-level", logLevelStr, "trace|debug|info|warn|error");
    cli_->add_option("--device", deviceStr, "Audio output device");
    std::vector<std::string> posPaths;
    bool posSave = false;
    cli_->add_option("path", posPaths, "Audio file, folder, or glob (direct play)");
    cli_->add_flag("--save", posSave, "Keep the direct-play queue (default: temporary)");
    bool daemonFlag = false;
    cli_->add_flag("--daemon", daemonFlag, "Internal daemon flag")->group("");
    bool globalFg = false;
    cli_->add_flag("--foreground", globalFg, "")->group("");
    bool fg = false;
    auto* startCmd = cli_->add_subcommand("start", "Start daemon");
    startCmd->add_flag("--foreground", fg, "Run in foreground");
    auto* shutdownCmd = cli_->add_subcommand("shutdown", "Stop daemon");
    bool playJson = false;
    auto* playCmd = cli_->add_subcommand("play", "Play current queue");
    playCmd->add_flag("--json", playJson, "JSON output");
    bool pauseJson = false;
    auto* pauseCmd = cli_->add_subcommand("pause", "Pause playback");
    pauseCmd->add_flag("--json", pauseJson, "JSON output");
    bool resumeJson = false;
    auto* resumeCmd = cli_->add_subcommand("resume", "Resume playback");
    resumeCmd->add_flag("--json", resumeJson, "JSON output");
    bool restartJson = false;
    auto* restartCmd = cli_->add_subcommand("restart", "Restart current track");
    restartCmd->add_flag("--json", restartJson, "JSON output");
    bool stopJson = false;
    auto* stopCmd = cli_->add_subcommand("stop", "Stop playback");
    stopCmd->add_flag("--json", stopJson, "JSON output");
    bool nextJson = false;
    auto* nextCmd = cli_->add_subcommand("next", "Next track");
    nextCmd->add_flag("--json", nextJson, "JSON output");
    bool prevJson = false;
    auto* prevCmd = cli_->add_subcommand("prev", "Prev track");
    prevCmd->add_flag("--json", prevJson, "JSON output");
    std::string seekStr;
    bool seekJson = false;
    auto* seekCmd = cli_->add_subcommand("seek", "Seek to position");
    seekCmd->add_option("time", seekStr, "mm:ss or seconds or +N/-N")->required();
    seekCmd->add_flag("--json", seekJson, "JSON output");
    bool jsonFlag = false;
    bool statusWatch = false;
    int statusInterval = 1000;
    auto* statusCmd = cli_->add_subcommand("status", "Show status");
    statusCmd->add_flag("--json", jsonFlag, "JSON output");
    statusCmd->add_flag("--watch", statusWatch, "Continuous polling");
    statusCmd->add_flag("--follow", statusWatch, "Alias for --watch");
    statusCmd->add_option("--interval", statusInterval, "Poll interval in ms");
    std::string volumeArg;
    bool volumeJson = false;
    auto* volumeCmd = cli_->add_subcommand("volume", "Get/set volume");
    volumeCmd->add_option("level", volumeArg, "0-100|+N|-N|mute|unmute");
    volumeCmd->add_flag("--json", volumeJson, "JSON output");
    auto* queueCmd = cli_->add_subcommand("queue", "Queue operations");
    bool qJson = false;
    auto* qList = queueCmd->add_subcommand("list", "List all queues");
    qList->add_flag("--json", qJson, "JSON output");
    bool qQueuesJson = false;
    auto* qQueuesAlias =
        queueCmd->add_subcommand("queues", "List all queues (alias)")->group("");
    qQueuesAlias->add_flag("--json", qQueuesJson, "JSON output");
    std::string qTracksOrder = "playback";
    bool qTracksJson = false;
    auto* qTracks = queueCmd->add_subcommand("tracks", "List tracks in active queue");
    qTracks->add_option("--order", qTracksOrder, "Track order: added|playback")
        ->check(CLI::IsMember({"added", "playback"}));
    qTracks->add_flag("--json", qTracksJson, "JSON output");
    std::int64_t qSwitchId = 0;
    bool qSwitchJson = false;
    auto* qSwitch = queueCmd->add_subcommand("switch", "Switch active queue");
    qSwitch->add_option("qid", qSwitchId, "Queue id")->required();
    qSwitch->add_flag("--json", qSwitchJson, "JSON output");
    std::vector<std::string> qAddPaths;
    std::string qAddId;
    bool qAddSearch = false;
    bool qAddRecursive = false;
    std::int64_t qAddPlaylist = 0;
    bool qAddReplace = false;
    bool qAddJson = false;
    auto* qAdd = queueCmd->add_subcommand("add", "Add to queue");
    qAdd->add_option("path", qAddPaths, "File, folder, or glob (repeatable)");
    qAdd->add_option("--id", qAddId, "Library track id");
    qAdd->add_flag("--search", qAddSearch, "Treat path as FTS query (single)");
    qAdd->add_flag("--recursive", qAddRecursive, "Recurse into folders");
    qAdd->add_option("--playlist", qAddPlaylist, "Playlist id to append");
    qAdd->add_flag("--replace", qAddReplace, "Clear queue first (with --playlist)");
    qAdd->add_flag("--json", qAddJson, "JSON output");
    std::vector<std::string> qRemovePaths;
    std::string qRemoveId;
    std::string qRemovePos;
    bool qRemoveRecursive = false;
    bool qRemoveJson = false;
    auto* qRemove = queueCmd->add_subcommand("remove", "Remove from queue");
    qRemove->add_option("path", qRemovePaths, "File, folder, or glob (repeatable)");
    qRemove->add_option("--id", qRemoveId, "Library track id");
    qRemove->add_option("--pos", qRemovePos, "Queue position");
    qRemove->add_flag("--recursive", qRemoveRecursive, "Recurse into folders");
    qRemove->add_flag("--json", qRemoveJson, "JSON output");
    std::string qCreateName;
    bool qCreateJson = false;
    auto* qCreate = queueCmd->add_subcommand("create", "Create a new queue");
    qCreate->add_option("name", qCreateName, "Queue name")->required();
    qCreate->add_flag("--json", qCreateJson, "JSON output");
    std::int64_t qDeleteQid = 0;
    bool qDeleteJson = false;
    auto* qDelete = queueCmd->add_subcommand("delete", "Delete a queue");
    qDelete->add_option("qid", qDeleteQid, "Queue id")->required();
    qDelete->add_flag("--json", qDeleteJson, "JSON output");
    std::size_t qFrom = 0, qTo = 0;
    bool qMoveJson = false;
    auto* qMove = queueCmd->add_subcommand("move", "Move within queue");
    qMove->add_option("from", qFrom, "from index")->required();
    qMove->add_option("to", qTo, "to index")->required();
    qMove->add_flag("--json", qMoveJson, "JSON output");
    bool qClearJson = false;
    auto* qClear = queueCmd->add_subcommand("clear", "Clear queue");
    qClear->add_flag("--json", qClearJson, "JSON output");
    std::string qShuffleArg;
    bool qShuffleJson = false;
    auto* qShuffle = queueCmd->add_subcommand("shuffle", "Set shuffle");
    qShuffle->add_option("mode", qShuffleArg, "on|off");
    qShuffle->add_flag("--json", qShuffleJson, "JSON output");
    std::string qRepeatArg;
    bool qRepeatJson = false;
    auto* qRepeat = queueCmd->add_subcommand("repeat", "Set repeat");
    qRepeat->add_option("mode", qRepeatArg, "off|one|all");
    qRepeat->add_flag("--json", qRepeatJson, "JSON output");
    auto* plCmd = cli_->add_subcommand("playlist", "Playlist operations");
    bool plJson = false;
    auto* plList = plCmd->add_subcommand("list", "List playlists");
    plList->add_flag("--json", plJson, "JSON output");
    std::int64_t plTracksPid = 0;
    bool plTracksJson = false;
    auto* plTracks = plCmd->add_subcommand("tracks", "Tracks in playlist");
    plTracks->add_option("pid", plTracksPid, "Playlist id")->required();
    plTracks->add_flag("--json", plTracksJson, "JSON output");
    std::string plCreateName;
    bool plCreateJson = false;
    auto* plCreate = plCmd->add_subcommand("create", "Create an empty playlist");
    plCreate->add_option("name", plCreateName, "Playlist name")->required();
    plCreate->add_flag("--json", plCreateJson, "JSON output");
    std::int64_t plAddPid = 0;
    std::vector<std::string> plAddIds;
    std::vector<std::string> plAddPaths;
    bool plAddRecursive = false;
    bool plAddJson = false;
    auto* plAdd = plCmd->add_subcommand("add", "Add tracks to playlist");
    plAdd->add_option("pid", plAddPid, "Playlist id")->required();
    plAdd->add_option("--id", plAddIds, "Library track id (repeatable)");
    plAdd->add_option("path", plAddPaths, "File, folder, or glob (repeatable)");
    plAdd->add_flag("--recursive", plAddRecursive, "Recurse into folders");
    plAdd->add_flag("--json", plAddJson, "JSON output");
    std::int64_t plLoadPid = 0;
    bool plLoadPlay = false;
    bool plLoadReplace = false;
    bool plLoadJson = false;
    auto* plLoad = plCmd->add_subcommand("load", "Load playlist into queue");
    plLoad->add_option("pid", plLoadPid, "Playlist id")->required();
    plLoad->add_flag("--play", plLoadPlay, "Play after load");
    plLoad->add_flag("--replace", plLoadReplace, "Replace active queue");
    plLoad->add_flag("--json", plLoadJson, "JSON output");
    std::string plSaveName;
    std::int64_t plSaveQid = 0;
    bool plSaveJson = false;
    auto* plSave = plCmd->add_subcommand("save", "Save queue as playlist");
    plSave->add_option("name", plSaveName, "Playlist name")->required();
    plSave->add_option("--queue", plSaveQid, "Queue id");
    plSave->add_flag("--json", plSaveJson, "JSON output");
    std::int64_t plDeletePid = 0;
    bool plDeleteJson = false;
    auto* plDelete = plCmd->add_subcommand("delete", "Delete playlist");
    plDelete->add_option("pid", plDeletePid, "Playlist id")->required();
    plDelete->add_flag("--json", plDeleteJson, "JSON output");
    std::int64_t plRenamePid = 0;
    std::string plRenameName;
    bool plRenameJson = false;
    auto* plRename = plCmd->add_subcommand("rename", "Rename playlist");
    plRename->add_option("pid", plRenamePid, "Playlist id")->required();
    plRename->add_option("name", plRenameName, "New name")->required();
    plRename->add_flag("--json", plRenameJson, "JSON output");
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
    bool plImportJson = false;
    auto* plImport = plCmd->add_subcommand("import", "Import playlist from file");
    plImport->add_option("path", plImportPath, "Input file path")->required();
    plImport->add_option("--name", plImportName, "Playlist name (default: filename)");
    plImport->add_flag("--json", plImportJson, "JSON output");
    auto* libCmd = cli_->add_subcommand("library", "Library operations");
    std::string libScanPath;
    bool libScanFullHash = false;
    bool libScanJson = false;
    auto* libScan = libCmd->add_subcommand("scan", "Scan library");
    libScan->add_option("--path", libScanPath, "Scan path (default: library music dir)");
    libScan->add_flag("--full-hash", libScanFullHash, "Full-file BLAKE3 (slower)");
    libScan->add_flag("--json", libScanJson, "JSON output");
    std::string libSearchQuery;
    int libSearchLimit = 50;
    bool libSearchJson = false;
    auto* libSearch = libCmd->add_subcommand("search", "Search library");
    libSearch->add_option("query", libSearchQuery, "FTS query")->required();
    libSearch->add_option("--limit", libSearchLimit, "Limit");
    libSearch->add_flag("--json", libSearchJson, "JSON output");
    bool libStatsJson = false;
    int libStatsMostPlayed = -1;
    std::vector<std::string> libStatsQueues;
    std::vector<std::int64_t> libStatsPlaylists;
    auto* libStats = libCmd->add_subcommand("stats", "Library stats");
    libStats->add_flag("--json", libStatsJson, "JSON output");
    libStats->add_option("--most-played", libStatsMostPlayed,
                         "Top-N most played (0 = totals only)");
    libStats->add_option("--queue", libStatsQueues, "Queue overview: all or id (repeatable)");
    libStats->add_option("--playlist", libStatsPlaylists, "Playlist overview by id (repeatable)");
    std::string libAddPath;
    bool libAddRecursive = false;
    bool libAddJson = false;
    auto* libAdd = libCmd->add_subcommand("add", "Add file or directory to library");
    libAdd->add_option("path", libAddPath, "File or directory path")->required();
    libAdd->add_flag("--recursive", libAddRecursive, "Recurse into subdirectories");
    libAdd->add_flag("--json", libAddJson, "JSON output");
    std::string libRemoveQuery;
    bool libRemoveJson = false;
    auto* libRemove = libCmd->add_subcommand("remove", "Remove track from library");
    libRemove->add_option("id", libRemoveQuery, "Track id or path")->required();
    libRemove->add_flag("--json", libRemoveJson, "JSON output");
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
    bool tagEditJson = false;
    auto* tagEdit = tagCmd->add_subcommand("edit", "Edit track tag");
    tagEdit->add_option("id", tagEditId, "Track id")->required();
    tagEdit
        ->add_option("field", tagEditField,
                     "Field (title,artist,album,album_artist,genre,year,track_number,disc_number)")
        ->required()
        ->check(CLI::IsMember({"title", "artist", "album", "album_artist", "genre", "year",
                               "track_number", "disc_number"}));
    tagEdit->add_option("value", tagEditValue, "New value")->required();
    tagEdit->add_flag("--json", tagEditJson, "JSON output");
    std::int64_t tagGetId = 0;
    std::string tagGetField;
    bool tagGetJson = false;
    auto* tagGet = tagCmd->add_subcommand("get", "Get track tags");
    tagGet->add_option("id", tagGetId, "Track id")->required();
    tagGet->add_option("field", tagGetField, "Single field (default: all)");
    tagGet->add_flag("--json", tagGetJson, "JSON output");
    auto* historyCmd = cli_->add_subcommand("history", "Playback history operations");
    bool historyJson = false;
    int historyLimit = 50;
    auto* historyList = historyCmd->add_subcommand("list", "List playback history");
    historyList->add_flag("--json", historyJson, "JSON output");
    historyList->add_option("--limit", historyLimit, "Limit entries");
    auto* historyClear = historyCmd->add_subcommand("clear", "Clear playback history");
    bool historyClearJson = false;
    historyClear->add_flag("--json", historyClearJson, "JSON output");
    std::string previewFile;
    auto* previewCmd = cli_->add_subcommand("preview", "Preview file (ephemeral)");
    previewCmd->add_option("file", previewFile, "File path")->required();
    previewCmd->group("");
    auto* cfgCmd = cli_->add_subcommand("config", "Config operations");
    std::string cfgGetKey;
    bool cfgGetJson = false;
    auto* cfgGet = cfgCmd->add_subcommand("get", "Get config value");
    cfgGet->add_option("key", cfgGetKey, "Key")->required();
    cfgGet->add_flag("--json", cfgGetJson, "JSON output");
    std::string cfgSetKey, cfgSetVal;
    auto* cfgSet = cfgCmd->add_subcommand("set", "Set config value");
    cfgSet->add_option("key", cfgSetKey, "Key")->required();
    cfgSet->add_option("value", cfgSetVal, "Value")->required();
    bool cfgListJson = false;
    auto* cfgList = cfgCmd->add_subcommand("list", "List config");
    cfgList->add_flag("--json", cfgListJson, "JSON output");
    std::string cfgExportPath;
    bool cfgExportJson = false;
    auto* cfgExport = cfgCmd->add_subcommand("export", "Export config");
    cfgExport->add_option("path", cfgExportPath, "Path")->required();
    cfgExport->add_flag("--json", cfgExportJson, "JSON output");
    std::string cfgImportPath;
    bool cfgImportJson = false;
    auto* cfgImport = cfgCmd->add_subcommand("import", "Import config");
    cfgImport->add_option("path", cfgImportPath, "Path")->required();
    cfgImport->add_flag("--json", cfgImportJson, "JSON output");
    std::string cfgResetKey;
    bool cfgResetJson = false;
    auto* cfgReset = cfgCmd->add_subcommand("reset", "Reset config to defaults");
    cfgReset->add_option("key", cfgResetKey, "Key to reset (omit to reset all)");
    cfgReset->add_flag("--json", cfgResetJson, "JSON output");
    // Device commands
    auto* deviceCmd = cli_->add_subcommand("device", "Audio device operations");
    bool devJson = false;
    auto* devList = deviceCmd->add_subcommand("list", "List audio output devices");
    devList->add_flag("--json", devJson, "JSON output");
    std::string devSetId;
    bool devSetJson = false;
    auto* devSet = deviceCmd->add_subcommand("set", "Set default audio device");
    devSet->add_option("id", devSetId, "Device ID")->required();
    devSet->add_flag("--json", devSetJson, "JSON output");
    std::string devTestId;
    bool devTestJson = false;
    auto* devTest = deviceCmd->add_subcommand("test", "Check audio device is available");
    devTest->add_option("--id", devTestId, "Device ID (default: current config)");
    devTest->add_flag("--json", devTestJson, "JSON output");
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
        else {
            caudio::println(std::cerr,
                            "--log-level: invalid '{}' (expected trace|debug|info|warn|error)",
                            logLevelStr);
            return 1;
        }
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
    // The App orchestrator was bound to the pre-parse config in the Shell
    // ctor; sync it now that reload plus socket derivation are final. Every
    // daemon interaction below must observe these paths.
    app_.setConfig(config_);
    // Internal daemon mode: if --daemon present, run Service foreground immediately (child process)
    if (daemonFlag) {
        return app_.startDaemon(true);
    }
    auto sendRaw = [&](const caudio::ipc::Command& cmd,
                       std::chrono::milliseconds timeout = std::chrono::milliseconds{
                           2000}) -> std::expected<caudio::ipc::Result, caudio::utils::Error> {
        caudio::client::Client client{config_.dbPath, config_.socketPath};
        auto res = client.send(cmd, timeout);
        if (!res)
            return std::unexpected{res.error()};
        if (std::holds_alternative<caudio::utils::Error>(*res))
            return std::unexpected{std::get<caudio::utils::Error>(*res)};
        return std::move(*res);
    };
    auto printErr = [&](const caudio::utils::Error& e) -> int {
        caudio::ipc::Result errRes{e};
        caudio::client::OutputFormatter fmt{false};
        fmt.print(errRes, std::cerr);
        if (e.code == caudio::utils::StatusCode::State && e.message == "daemon not running")
            caudio::println(std::cerr, "hint: run `caudio start` to start the daemon");
        return 1;
    };
    auto printJson = [&](const caudio::ipc::Result& r) -> int {
        caudio::client::OutputFormatter fmt{true};
        fmt.print(r, std::cout);
        return 0;
    };
    auto sendViaClient = [&](const caudio::ipc::Command& cmd, bool asJson) -> int {
        auto res = sendRaw(cmd);
        if (!res)
            return printErr(res.error());
        if (asJson)
            return printJson(*res);
        caudio::client::OutputFormatter fmt{false};
        fmt.print(*res, std::cout);
        return 0;
    };
    // Play-like sends autostart the daemon when it is down (quick launch).
    // quietAutostart keeps --json output clean.
    auto sendPlay = [&](const caudio::ipc::Command& cmd, bool quietAutostart)
        -> std::expected<caudio::ipc::Result, caudio::utils::Error> {
        auto res = sendRaw(cmd);
        if (!res) {
            const auto& e = res.error();
            if (e.code == caudio::utils::StatusCode::State && e.message == "daemon not running") {
                if (app_.startDaemon(false, quietAutostart) != 0) {
                    // The autostart may have raced a dying daemon (lock held
                    // at spawn). One more attempt after a short settle delay;
                    // surface the freshest error, not the original "down".
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    auto retry = sendRaw(cmd);
                    if (retry)
                        return retry;
                    return std::unexpected{retry.error()};
                }
                return sendRaw(cmd);
            }
        }
        return res;
    };
    // Custom confirmation: JSON dumps the raw result, text prints `line`.
    auto confirm = [&](std::expected<caudio::ipc::Result, caudio::utils::Error>&& res, bool asJson,
                       const std::string& line) -> int {
        if (!res)
            return printErr(res.error());
        if (asJson)
            return printJson(*res);
        caudio::println(std::cout, "{}", line);
        return 0;
    };
    // Direct play: `caudio PATH... [--save]` with no subcommand. Files are
    // fingerprinted into the library; the queue itself is temporary unless
    // --save keeps it. Output is human-readable text.
    bool anySub = false;
    for (auto* sc : cli_->get_subcommands()) {
        if (sc->parsed()) {
            anySub = true;
            break;
        }
    }
    if (!posPaths.empty() || posSave) {
        if (anySub) {
            caudio::println(std::cerr, "paths and --save take no subcommand");
            return 1;
        }
        if (posPaths.empty()) {
            caudio::println(std::cerr, "need a PATH (audio file, folder, or glob)");
            return 1;
        }
        std::vector<std::string> files;
        std::vector<std::string> unmatched;
        for (auto& tok : posPaths)
            detail::expandAddToken(tok, false, files, unmatched);
        bool hardFail = false;
        for (auto& u : unmatched) {
            if (detail::hasGlobChars(u)) {
                caudio::println(std::cerr, "No files matched: {}", u);
            } else {
                std::error_code ec;
                if (std::filesystem::is_directory(u, ec) && !ec)
                    caudio::println(std::cerr, "No files matched: {}", u);
                else {
                    caudio::println(std::cerr, "no such file: {}", u);
                    hardFail = true;
                }
            }
        }
        if (files.empty())
            return hardFail ? 1 : 0;
        caudio::ipc::Command cmd{caudio::ipc::PlayFiles{files, posSave}};
        auto res = sendPlay(cmd, false);
        if (!res)
            return printErr(res.error());
        if (auto* st = std::get_if<caudio::ipc::Status>(&*res)) {
            std::string who = detail::trackWho(*st);
            if (posSave)
                caudio::println("Playing {}", who);
            else
                caudio::println("Playing {} (temporary queue)", who);
            return 0;
        }
        caudio::client::OutputFormatter fmt{false};
        fmt.print(*res, std::cout);
        return 0;
    }
    if (startCmd->parsed())
        return app_.startDaemon(fg || globalFg);
    if (shutdownCmd->parsed())
        return app_.shutdownDaemon();
    // Transport commands print one-line confirmations (no ids, no queue
    // positions -- see `info` / `queue list` for those). JSON dumps raw.
    auto confirmTransport = [&](std::expected<caudio::ipc::Result, caudio::utils::Error>&& res,
                                bool asJson, const std::string& line) -> int {
        if (!res)
            return printErr(res.error());
        if (asJson)
            return printJson(*res);
        caudio::println(std::cout, "{}", line);
        return 0;
    };
    auto transportWho = [&](std::expected<caudio::ipc::Result, caudio::utils::Error>& res) {
        if (auto* st = std::get_if<caudio::ipc::Status>(&*res))
            return detail::trackWho(*st);
        return std::string{"unknown track"};
    };
    // Shared play flow (used by `play`, and by `resume` when stopped).
    auto doPlay = [&](bool asJson) -> int {
        // Prior state decides the wording (resumed vs fresh); one extra roundtrip.
        bool wasPaused = false;
        double priorPos = 0;
        {
            caudio::client::Client probe{config_.dbPath, config_.socketPath};
            if (auto ps = probe.send(caudio::ipc::Command{caudio::ipc::StatusReq{}})) {
                if (auto* st = std::get_if<caudio::ipc::Status>(&*ps)) {
                    wasPaused = (st->state == caudio::engine::PlaybackState::Paused);
                    priorPos = st->pos;
                }
            }
        }
        caudio::ipc::Command cmd{caudio::ipc::Play{}};
        auto res = sendPlay(cmd, asJson);
        if (!res)
            return printErr(res.error());
        if (asJson)
            return printJson(*res);
        if (std::get_if<caudio::ipc::Status>(&*res)) {
            std::string who = transportWho(res);
            if (wasPaused)
                caudio::println("Resuming {} from {}", who, detail::fmtClock(priorPos));
            else
                caudio::println("Playing {}", who);
        } else {
            caudio::client::OutputFormatter fmt{false};
            fmt.print(*res, std::cout);
        }
        return 0;
    };
    if (playCmd->parsed())
        return doPlay(playJson);
    if (pauseCmd->parsed()) {
        caudio::ipc::Command cmd{caudio::ipc::Pause{}};
        auto res = sendRaw(cmd);
        if (!res) {
            const auto& e = res.error();
            if (e.code == caudio::utils::StatusCode::State && e.message == "not playing") {
                caudio::println(std::cerr, "pause: nothing playing");
                return 0;
            }
            return printErr(e);
        }
        if (pauseJson)
            return printJson(*res);
        if (std::get_if<caudio::ipc::Status>(&*res)) {
            std::string who = transportWho(res);
            double at = 0;
            if (auto* st = std::get_if<caudio::ipc::Status>(&*res))
                at = st->pos;
            caudio::println("Paused {} at {}", who, detail::fmtClock(at));
        } else {
            caudio::client::OutputFormatter fmt{false};
            fmt.print(*res, std::cout);
        }
        return 0;
    }
    if (resumeCmd->parsed()) {
        caudio::ipc::Command cmd{caudio::ipc::Resume{}};
        auto res = sendRaw(cmd);
        if (!res) {
            const auto& e = res.error();
            if (e.code == caudio::utils::StatusCode::State && e.message == "not paused") {
                // Forgiving resume: playing -> warn; stopped -> play from cursor.
                bool playing = false;
                bool probed = false;
                caudio::client::Client probe{config_.dbPath, config_.socketPath};
                if (auto ps = probe.send(caudio::ipc::Command{caudio::ipc::StatusReq{}})) {
                    if (auto* st = std::get_if<caudio::ipc::Status>(&*ps)) {
                        probed = true;
                        playing = (st->state == caudio::engine::PlaybackState::Playing);
                    }
                }
                if (probed && playing) {
                    caudio::println(std::cerr, "resume: already playing");
                    return 0;
                }
                if (!probed)
                    return printErr(e);
                return doPlay(resumeJson);
            }
            return printErr(e);
        }
        if (resumeJson)
            return printJson(*res);
        if (auto* st = std::get_if<caudio::ipc::Status>(&*res)) {
            caudio::println("Resuming {} from {}", detail::trackWho(*st),
                            detail::fmtClock(st->pos));
        } else {
            caudio::client::OutputFormatter fmt{false};
            fmt.print(*res, std::cout);
        }
        return 0;
    }
    if (restartCmd->parsed()) {
        caudio::ipc::Command cmd{caudio::ipc::Restart{}};
        auto res = sendRaw(cmd);
        if (!res)
            return printErr(res.error());
        if (restartJson)
            return printJson(*res);
        caudio::println("Restarting {}", transportWho(res));
        return 0;
    }
    if (stopCmd->parsed()) {
        caudio::ipc::Command cmd{caudio::ipc::Stop{}};
        return confirmTransport(sendRaw(cmd), stopJson, "Stopped");
    }
    if (nextCmd->parsed()) {
        caudio::ipc::Command cmd{caudio::ipc::Next{}};
        auto res = sendRaw(cmd);
        if (!res)
            return printErr(res.error());
        if (nextJson)
            return printJson(*res);
        caudio::println("Playing {}", transportWho(res));
        return 0;
    }
    if (prevCmd->parsed()) {
        caudio::ipc::Command cmd{caudio::ipc::Prev{}};
        auto res = sendRaw(cmd);
        if (!res) {
            const auto& e = res.error();
            if (e.code == caudio::utils::StatusCode::NotFound && e.message == "at start") {
                caudio::println(std::cerr, "prev: at queue start");
                return 0;
            }
            return printErr(e);
        }
        if (prevJson)
            return printJson(*res);
        caudio::println("Playing {}", transportWho(res));
        return 0;
    }
    if (seekCmd->parsed()) {
        auto parsed = detail::parseSeek(seekStr);
        if (!parsed) {
            caudio::println(std::cerr,
                            "seek: '{}': {} (expected mm:ss, hh:mm:ss, seconds, or +N/-N)", seekStr,
                            parsed.error().message);
            return 1;
        }
        double target = *parsed;
        // parseSeek trims whitespace, so detect +/- on the trimmed form too:
        // " +5" is a +5 delta, not absolute 5.
        std::string_view sv = seekStr;
        while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t'))
            sv.remove_prefix(1);
        bool isRelative = !sv.empty() && (sv.front() == '+' || sv.front() == '-');
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
        auto res = sendRaw(cmd);
        if (!res)
            return printErr(res.error());
        if (seekJson)
            return printJson(*res);
        return 0;
    }
    if (statusCmd->parsed()) {
        if (statusWatch) {
            if (statusInterval <= 0) {
                caudio::println(std::cerr, "status: --interval must be positive (got {})",
                                statusInterval);
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
                    // caudio::print(std::cout, output);
                    if (auto* st = std::get_if<caudio::ipc::Status>(&*res)) {
                        caudio::print(std::cout, "\r\33[2K[{}] {}", buf, detail::statusLine(*st));
                        std::cout.flush();
                    } else {
                        caudio::println(std::cout, "[{}]", buf);
                        fmt.print(*res, std::cout);
                    }
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
            caudio::println(std::cerr, "volume: '{}': {} (expected 0-100, +N/-N, mute, or unmute)",
                            volumeArg, pv.error().message);
            return 1;
        }
        caudio::ipc::Command cmd{*pv};
        return sendViaClient(cmd, volumeJson);
    }
    // Print Added lines for a QueueTracks result, warning on ids already seen
    // (pre-existing queue members). Returns the newly added count.
    auto printAdded = [&](const caudio::ipc::Result& res, std::set<int64_t>& seen) -> int {
        auto* qt = std::get_if<caudio::ipc::QueueTracks>(&res);
        if (!qt) {
            caudio::client::OutputFormatter fmt{false};
            fmt.print(res, std::cout);
            return 0;
        }
        int added = 0;
        for (auto& t : qt->tracks) {
            std::string label = detail::addedLabel(t);
            if (seen.contains(t.id)) {
                caudio::println(std::cerr, "already in queue: {}", label);
            } else {
                caudio::println("Added {}", label);
                seen.insert(t.id);
                ++added;
            }
        }
        return added;
    };
    auto countLine = [&](int added) {
        if (added == 1)
            caudio::println("1 track added");
        else
            caudio::println("{} tracks added", added);
    };
    auto isNumeric = [](const std::string& s) {
        if (s.empty())
            return false;
        for (char c : s) {
            if (!std::isdigit((unsigned char)c))
                return false;
        }
        return true;
    };
    if (queueCmd->parsed()) {
        if (qList->parsed() || qQueuesAlias->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::QueueQueues{}};
            return sendViaClient(cmd, qList->parsed() ? qJson : qQueuesJson);
        }
        if (qTracks->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::QueueList{qTracksOrder}};
            auto res = sendRaw(cmd);
            if (!res)
                return printErr(res.error());
            if (qTracksJson)
                return printJson(*res);
            int64_t curId = 0;
            caudio::client::Client probe{config_.dbPath, config_.socketPath};
            if (auto ps = probe.send(caudio::ipc::Command{caudio::ipc::StatusReq{}})) {
                if (auto* st = std::get_if<caudio::ipc::Status>(&*ps))
                    curId = st->track_id;
            }
            caudio::client::OutputFormatter fmt{false, detail::useColor()};
            fmt.setHighlightTrackId(curId);
            fmt.print(*res, std::cout);
            return 0;
        }
        if (qSwitch->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::QueueSwitch{qSwitchId}};
            return confirm(sendRaw(cmd), qSwitchJson,
                           std::format("Switched to queue {}", qSwitchId));
        }
        if (qCreate->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::QueueCreate{qCreateName}};
            auto res = sendRaw(cmd);
            if (!res)
                return printErr(res.error());
            if (qCreateJson)
                return printJson(*res);
            if (auto* qc = std::get_if<caudio::ipc::QueueCreated>(&*res)) {
                caudio::println("Created queue {} '{}'", qc->id, qc->name);
                return 0;
            }
            caudio::client::OutputFormatter fmt{false};
            fmt.print(*res, std::cout);
            return 0;
        }
        if (qDelete->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::QueueDelete{qDeleteQid}};
            return confirm(sendRaw(cmd), qDeleteJson,
                           std::format("Deleted queue {}", qDeleteQid));
        }
        if (qAdd->parsed()) {
            bool hasId = !qAddId.empty();
            bool hasPlaylist = qAddPlaylist != 0;
            if (qAddReplace && !hasPlaylist) {
                caudio::println(std::cerr, "queue add: --replace needs --playlist");
                return 1;
            }
            if (hasPlaylist && (hasId || qAddSearch || !qAddPaths.empty())) {
                caudio::println(std::cerr, "queue add: --playlist takes no PATH, --id, or --search");
                return 1;
            }
            if (hasId && (!qAddPaths.empty() || qAddSearch)) {
                caudio::println(std::cerr, "queue add: --id takes no PATH or --search");
                return 1;
            }
            if (qAddSearch && qAddPaths.size() != 1) {
                caudio::println(std::cerr, "queue add: --search takes exactly one query");
                return 1;
            }
            if (hasId && !isNumeric(qAddId)) {
                caudio::println(std::cerr, "queue add: --id needs a numeric library id");
                return 1;
            }
            if (!hasId && !qAddSearch && !hasPlaylist && qAddPaths.empty()) {
                caudio::println(std::cerr, "queue add: need a PATH, --id ID, or --search QUERY");
                return 1;
            }
            if (hasPlaylist && qAddReplace) {
                auto clr = sendRaw(caudio::ipc::Command{caudio::ipc::QueueClear{}});
                if (!clr)
                    return printErr(clr.error());
            }
            // Seed the known-id set so re-adds warn instead of duplicating.
            std::set<int64_t> seen;
            {
                caudio::client::Client probe{config_.dbPath, config_.socketPath};
                if (auto ps = probe.send(caudio::ipc::Command{caudio::ipc::QueueList{"added"}})) {
                    if (auto* qt = std::get_if<caudio::ipc::QueueTracks>(&*ps)) {
                        for (auto& t : qt->tracks)
                            seen.insert(t.id);
                    }
                }
            }
            if (hasPlaylist) {
                caudio::ipc::Command tcmd{caudio::ipc::PlaylistTracks{qAddPlaylist}};
                auto tres = sendRaw(tcmd);
                if (!tres)
                    return printErr(tres.error());
                auto* pd = std::get_if<caudio::ipc::PlaylistData>(&*tres);
                if (!pd) {
                    caudio::client::OutputFormatter fmt{false};
                    fmt.print(*tres, std::cout);
                    return 0;
                }
                if (pd->tracks.empty()) {
                    caudio::println(std::cerr, "queue add: playlist {} has no tracks",
                                    qAddPlaylist);
                    return 1;
                }
                int added = 0;
                caudio::ipc::QueueTracks collected{};
                for (auto& t : pd->tracks) {
                    caudio::ipc::Command cmd{caudio::ipc::QueueAdd{std::to_string(t.id), false}};
                    auto res = sendRaw(cmd);
                    if (!res) {
                        printErr(res.error());
                        continue;
                    }
                    if (qAddJson) {
                        if (auto* qt = std::get_if<caudio::ipc::QueueTracks>(&*res)) {
                            for (auto& at : qt->tracks)
                                collected.tracks.push_back(at);
                        }
                    } else {
                        added += printAdded(*res, seen);
                    }
                }
                if (qAddJson)
                    return printJson(caudio::ipc::Result{std::move(collected)});
                countLine(added);
                return (added == 0) ? 1 : 0;
            }
            if (hasId || qAddSearch) {
                caudio::ipc::Command cmd{
                    caudio::ipc::QueueAdd{hasId ? qAddId : qAddPaths[0], qAddSearch}};
                auto res = sendRaw(cmd);
                if (!res)
                    return printErr(res.error());
                if (qAddJson)
                    return printJson(*res);
                int added = printAdded(*res, seen);
                countLine(added);
                return 0;
            }
            // PATH mode: expand globs/folders client-side.
            std::vector<std::string> files;
            std::vector<std::string> unmatched;
            for (auto& tok : qAddPaths)
                detail::expandAddToken(tok, qAddRecursive, files, unmatched);
            bool hardFail = false;
            for (auto& u : unmatched) {
                if (isNumeric(u)) {
                    caudio::println(std::cerr,
                                    "queue add: '{}' is not a file (use --id for library ids)", u);
                    hardFail = true;
                } else if (detail::hasGlobChars(u)) {
                    caudio::println(std::cerr, "No files matched: {}", u);
                } else {
                    std::error_code ec;
                    if (std::filesystem::is_directory(u, ec) && !ec)
                        caudio::println(std::cerr, "No files matched: {}", u);
                    else {
                        caudio::println(std::cerr, "queue add: no such file: {}", u);
                        hardFail = true;
                    }
                }
            }
            int added = 0;
            caudio::ipc::QueueTracks collected{};
            for (auto& f : files) {
                caudio::ipc::Command cmd{caudio::ipc::QueueAdd{f, false}};
                auto res = sendRaw(cmd);
                if (!res) {
                    printErr(res.error());
                    hardFail = true;
                    continue;
                }
                if (qAddJson) {
                    if (auto* qt = std::get_if<caudio::ipc::QueueTracks>(&*res)) {
                        for (auto& t : qt->tracks)
                            collected.tracks.push_back(t);
                    }
                } else {
                    added += printAdded(*res, seen);
                }
            }
            if (qAddJson)
                return printJson(caudio::ipc::Result{std::move(collected)});
            countLine(added);
            if (added == 0 && hardFail)
                return 1;
            return 0;
        }
        if (qRemove->parsed()) {
            bool hasId = !qRemoveId.empty();
            bool hasPos = !qRemovePos.empty();
            bool hasPaths = !qRemovePaths.empty();
            int modes = (hasId ? 1 : 0) + (hasPos ? 1 : 0) + (hasPaths ? 1 : 0);
            if (modes == 0) {
                caudio::println(std::cerr, "queue remove: need PATH, --id ID, or --pos POS");
                return 1;
            }
            if (modes > 1) {
                caudio::println(std::cerr, "queue remove: PATH, --id, and --pos are exclusive");
                return 1;
            }
            if ((hasId && !isNumeric(qRemoveId)) || (hasPos && !isNumeric(qRemovePos))) {
                caudio::println(std::cerr, "queue remove: --id and --pos need numeric values");
                return 1;
            }
            // Snapshot insertion-order positions with labels for output.
            struct RemTarget {
                std::size_t pos{0};
                caudio::db::Track track{};
            };
            std::vector<RemTarget> all;
            {
                caudio::client::Client probe{config_.dbPath, config_.socketPath};
                auto ps = probe.send(caudio::ipc::Command{caudio::ipc::QueueList{"added"}});
                if (!ps)
                    return printErr(ps.error());
                if (auto* qt = std::get_if<caudio::ipc::QueueTracks>(&*ps)) {
                    for (std::size_t i = 0; i < qt->tracks.size(); ++i)
                        all.push_back(RemTarget{i, qt->tracks[i]});
                } else {
                    caudio::client::OutputFormatter fmt{false};
                    fmt.print(*ps, std::cout);
                    return 0;
                }
            }
            std::vector<RemTarget> targets;
            bool hardFail = false;
            auto parseNum = [&](const std::string& s, long long& out) {
                try {
                    out = std::stoll(s);
                    return true;
                } catch (...) {
                    caudio::println(std::cerr, "queue remove: value out of range: {}", s);
                    return false;
                }
            };
            if (hasPos) {
                long long p = 0;
                if (!parseNum(qRemovePos, p))
                    return 1;
                if (p < 0 || (std::size_t)p >= all.size()) {
                    caudio::println(std::cerr, "queue remove: position out of range: {}", p);
                    return 1;
                }
                targets.push_back(all[(std::size_t)p]);
            } else if (hasId) {
                long long id = 0;
                if (!parseNum(qRemoveId, id))
                    return 1;
                for (auto& t : all) {
                    if (t.track.id == id)
                        targets.push_back(t);
                }
                if (targets.empty()) {
                    caudio::println(std::cerr, "queue remove: track {} is not in the queue", id);
                    return 1;
                }
            } else {
                std::vector<std::string> files;
                std::vector<std::string> unmatched;
                for (auto& tok : qRemovePaths)
                    detail::expandAddToken(tok, qRemoveRecursive, files, unmatched);
                for (auto& u : unmatched) {
                    if (isNumeric(u)) {
                        caudio::println(
                            std::cerr,
                            "queue remove: '{}' is not a file (use --id/--pos for ids)",
                            u);
                        hardFail = true;
                    } else if (detail::hasGlobChars(u)) {
                        caudio::println(std::cerr, "No files matched: {}", u);
                    } else {
                        std::error_code ec;
                        if (std::filesystem::is_directory(u, ec) && !ec)
                            caudio::println(std::cerr, "No files matched: {}", u);
                        else {
                            caudio::println(std::cerr, "queue remove: no such file: {}", u);
                            hardFail = true;
                        }
                    }
                }
                std::vector<bool> taken(all.size(), false);
                for (auto& f : files) {
                    std::string key = detail::pathKey(f);
                    bool found = false;
                    for (std::size_t i = 0; i < all.size(); ++i) {
                        if (!taken[i] && detail::pathKey(all[i].track.path) == key) {
                            targets.push_back(all[i]);
                            taken[i] = true;
                            found = true;
                        }
                    }
                    if (!found) {
                        caudio::println(std::cerr, "queue remove: not in queue: {}", f);
                        hardFail = true;
                    }
                }
            }
            // Remove descending so positions stay valid.
            std::sort(targets.begin(), targets.end(), [](const RemTarget& a, const RemTarget& b) {
                return a.pos > b.pos;
            });
            int removed = 0;
            std::vector<caudio::db::Track> removedTracks;
            for (auto& t : targets) {
                caudio::ipc::Command cmd{caudio::ipc::QueueRemove{std::to_string(t.pos)}};
                auto res = sendRaw(cmd);
                if (!res) {
                    printErr(res.error());
                    hardFail = true;
                    continue;
                }
                if (!qRemoveJson)
                    caudio::println("Removed from queue {}", detail::addedLabel(t.track));
                removedTracks.push_back(t.track);
                ++removed;
            }
            if (qRemoveJson)
                return printJson(
                    caudio::ipc::Result{caudio::ipc::QueueTracks{std::move(removedTracks)}});
            if (removed == 0)
                return (targets.empty() && !hardFail) ? 0 : 1;
            return 0;
        }
        if (qMove->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::QueueMove{qFrom, qTo}};
            auto res = sendRaw(cmd);
            if (!res)
                return printErr(res.error());
            if (qMoveJson)
                return printJson(*res);
            std::size_t n = 0;
            if (auto* qt = std::get_if<caudio::ipc::QueueTracks>(&*res))
                n = qt->tracks.size();
            caudio::println("Moved to position {}. Queue: {} tracks", qTo, n);
            return 0;
        }
        if (qClear->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::QueueClear{}};
            return confirm(sendRaw(cmd), qClearJson, "Queue cleared");
        }
        if (qShuffle->parsed()) {
            std::optional<bool> on;
            if (qShuffleArg == "on")
                on = true;
            else if (qShuffleArg == "off")
                on = false;
            else if (!qShuffleArg.empty()) {
                caudio::println(std::cerr, "shuffle: invalid mode '{}' (expected on|off)",
                                qShuffleArg);
                return 1;
            }
            caudio::ipc::Command cmd{caudio::ipc::QueueShuffle{on}};
            auto res = sendRaw(cmd);
            if (!res)
                return printErr(res.error());
            if (qShuffleJson)
                return printJson(*res);
            // Report the resulting state (the daemon answers Status).
            bool stateOn = on.value_or(false);
            bool known = on.has_value();
            if (auto* st = std::get_if<caudio::ipc::Status>(&*res)) {
                stateOn = st->shuffle;
                known = true;
            }
            if (!known) {
                caudio::println("Shuffle toggled");
                return 0;
            }
            caudio::println("Shuffle: {}", stateOn ? "on" : "off");
            return 0;
        }
        if (qRepeat->parsed()) {
            using RM = caudio::engine::RepeatMode;
            std::optional<RM> m;
            if (qRepeatArg == "off")
                m = RM::Off;
            else if (qRepeatArg == "one")
                m = RM::One;
            else if (qRepeatArg == "all")
                m = RM::All;
            else if (!qRepeatArg.empty()) {
                caudio::println(std::cerr, "repeat: invalid mode '{}' (expected off|one|all)",
                                qRepeatArg);
                return 1;
            } else {
                // Bare repeat cycles off -> all -> one -> off.
                caudio::client::Client probe{config_.dbPath, config_.socketPath};
                if (auto ps = probe.send(caudio::ipc::Command{caudio::ipc::StatusReq{}})) {
                    if (auto* st = std::get_if<caudio::ipc::Status>(&*ps)) {
                        if (st->repeat == RM::Off)
                            m = RM::All;
                        else if (st->repeat == RM::All)
                            m = RM::One;
                        else
                            m = RM::Off;
                    }
                }
                if (!m.has_value()) {
                    caudio::println(std::cerr, "repeat: could not read current mode");
                    return 1;
                }
            }
            caudio::ipc::Command cmd{caudio::ipc::QueueRepeat{m}};
            auto res = sendRaw(cmd);
            if (!res)
                return printErr(res.error());
            if (qRepeatJson)
                return printJson(*res);
            RM finalMode = *m;
            if (auto* st = std::get_if<caudio::ipc::Status>(&*res))
                finalMode = st->repeat;
            caudio::println("Repeat: {}",
                            finalMode == RM::All ? "all" : finalMode == RM::One ? "one" : "off");
            return 0;
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
            return sendViaClient(cmd, plTracksJson);
        }
        if (plCreate->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::PlaylistCreate{plCreateName}};
            auto res = sendRaw(cmd);
            if (!res)
                return printErr(res.error());
            if (plCreateJson)
                return printJson(*res);
            if (auto* pc = std::get_if<caudio::ipc::PlaylistCreated>(&*res)) {
                caudio::println("Created playlist {} '{}'", pc->id, pc->name);
                return 0;
            }
            caudio::client::OutputFormatter fmt{false};
            fmt.print(*res, std::cout);
            return 0;
        }
        if (plAdd->parsed()) {
            if (plAddIds.empty() && plAddPaths.empty()) {
                caudio::println(std::cerr, "playlist add: need --id ID or PATH");
                return 1;
            }
            std::vector<int64_t> ids;
            for (auto& s : plAddIds) {
                if (!isNumeric(s)) {
                    caudio::println(std::cerr, "playlist add: --id needs numeric ids");
                    return 1;
                }
                try {
                    ids.push_back(std::stoll(s));
                } catch (...) {
                    caudio::println(std::cerr, "playlist add: id out of range: {}", s);
                    return 1;
                }
            }
            // PATHs: expand, ensure library rows, resolve exact ids.
            std::vector<std::string> files;
            std::vector<std::string> unmatched;
            for (auto& tok : plAddPaths)
                detail::expandAddToken(tok, plAddRecursive, files, unmatched);
            bool hardFail = false;
            for (auto& u : unmatched) {
                if (isNumeric(u)) {
                    caudio::println(std::cerr,
                                    "playlist add: '{}' is not a file (use --id for ids)", u);
                    hardFail = true;
                } else if (detail::hasGlobChars(u)) {
                    caudio::println(std::cerr, "No files matched: {}", u);
                } else {
                    std::error_code ec;
                    if (std::filesystem::is_directory(u, ec) && !ec)
                        caudio::println(std::cerr, "No files matched: {}", u);
                    else {
                        caudio::println(std::cerr, "playlist add: no such file: {}", u);
                        hardFail = true;
                    }
                }
            }
            for (auto& f : files) {
                caudio::ipc::Command acmd{caudio::ipc::LibraryAdd{f, false}};
                auto ares = sendRaw(acmd);
                if (!ares) {
                    printErr(ares.error());
                    hardFail = true;
                    continue;
                }
                std::string fn = std::filesystem::path(f).filename().generic_string();
                caudio::ipc::Command scmd{caudio::ipc::LibrarySearch{fn, 50}};
                auto sres = sendRaw(scmd);
                if (!sres) {
                    printErr(sres.error());
                    hardFail = true;
                    continue;
                }
                auto* sr = std::get_if<caudio::ipc::SearchResults>(&*sres);
                if (!sr) {
                    caudio::client::OutputFormatter fmt{false};
                    fmt.print(*sres, std::cout);
                    hardFail = true;
                    continue;
                }
                bool found = false;
                for (auto& t : sr->tracks) {
                    if (detail::pathKey(t.path) == detail::pathKey(f)) {
                        ids.push_back(t.id);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    caudio::println(std::cerr, "playlist add: not in library: {}", f);
                    hardFail = true;
                }
            }
            int added = 0;
            caudio::ipc::QueueTracks collected{};
            for (auto tid : ids) {
                caudio::ipc::Command cmd{caudio::ipc::PlaylistAdd{plAddPid, tid}};
                auto res = sendRaw(cmd);
                if (!res) {
                    const auto& e = res.error();
                    if (e.code == caudio::utils::StatusCode::AlreadyExists) {
                        caudio::println(std::cerr, "already on playlist: track {}", tid);
                        continue;
                    }
                    printErr(e);
                    hardFail = true;
                    continue;
                }
                if (plAddJson) {
                    caudio::ipc::Command gcmd{caudio::ipc::TagGet{tid}};
                    if (auto gres = sendRaw(gcmd)) {
                        if (auto* st = std::get_if<caudio::ipc::SingleTrack>(&*gres))
                            collected.tracks.push_back(st->track);
                    }
                } else {
                    std::string label;
                    caudio::ipc::Command gcmd{caudio::ipc::TagGet{tid}};
                    if (auto gres = sendRaw(gcmd)) {
                        if (auto* st = std::get_if<caudio::ipc::SingleTrack>(&*gres))
                            label = detail::addedLabel(st->track);
                    }
                    if (label.empty())
                        label = "track " + std::to_string(tid);
                    caudio::println("Added {} to playlist {}", label, plAddPid);
                }
                ++added;
            }
            if (plAddJson)
                return printJson(caudio::ipc::Result{std::move(collected)});
            if (added == 1)
                caudio::println("1 track added");
            else
                caudio::println("{} tracks added", added);
            if (added == 0 && hardFail)
                return 1;
            return 0;
        }
        if (plLoad->parsed()) {
            caudio::ipc::Command cmd{
                caudio::ipc::PlaylistLoad{plLoadPid, plLoadPlay, plLoadReplace}};
            auto res = sendRaw(cmd);
            if (!res)
                return printErr(res.error());
            if (plLoadJson)
                return printJson(*res);
            if (auto* pl = std::get_if<caudio::ipc::PlaylistLoaded>(&*res)) {
                if (plLoadReplace)
                    caudio::println("Replaced queue {} with playlist {}", pl->queue_id,
                                    plLoadPid);
                else
                    caudio::println("Loaded playlist {} into queue {}", plLoadPid,
                                    pl->queue_id);
                if (plLoadPlay)
                    caudio::println("Playing {}", detail::trackWho(pl->status));
                return 0;
            }
            caudio::client::OutputFormatter fmt{false};
            fmt.print(*res, std::cout);
            return 0;
        }
        if (plSave->parsed()) {
            std::optional<std::int64_t> qid;
            if (plSave->get_option("--queue")->count() > 0)
                qid = plSaveQid;
            caudio::ipc::Command cmd{caudio::ipc::PlaylistSave{plSaveName, qid}};
            return confirm(sendRaw(cmd), plSaveJson,
                           std::format("Saved playlist '{}'", plSaveName));
        }
        if (plDelete->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::PlaylistDelete{plDeletePid}};
            return confirm(sendRaw(cmd), plDeleteJson,
                           std::format("Deleted playlist {}", plDeletePid));
        }
        if (plRename->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::PlaylistRename{plRenamePid, plRenameName}};
            return confirm(sendRaw(cmd), plRenameJson,
                           std::format("Renamed playlist {} to '{}'", plRenamePid, plRenameName));
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
                    caudio::println(std::cerr, "export: failed to open output file '{}'",
                                    plExportPath);
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
            caudio::println(std::cerr, "export: unexpected response from daemon");
            return 1;
        }
        if (plImport->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::PlaylistImport{
                plImportPath, plImportName.empty() ? std::optional<std::string>{}
                                                   : std::optional<std::string>{plImportName}}};
            return sendViaClient(cmd, plImportJson);
        }
        std::cout << plCmd->help() << "\n";
        return 0;
    }
    if (libCmd->parsed()) {
        if (libScan->parsed()) {
            std::optional<std::string> p;
            if (!libScanPath.empty())
                p = libScanPath;
            caudio::ipc::Command cmd{caudio::ipc::LibraryScan{p, libScanFullHash}};
            return sendViaClient(cmd, libScanJson);
        }
        if (libSearch->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::LibrarySearch{libSearchQuery, libSearchLimit}};
            auto res = sendRaw(cmd);
            if (!res)
                return printErr(res.error());
            if (libSearchJson)
                return printJson(*res);
            caudio::client::OutputFormatter fmt{false, detail::useColor()};
            fmt.setHighlightNeedle(libSearchQuery);
            fmt.print(*res, std::cout);
            return 0;
        }
        if (libStats->parsed()) {
            bool detailed = libStatsMostPlayed >= 0;
            if (libStatsJson && (!libStatsQueues.empty() || !libStatsPlaylists.empty())) {
                caudio::println(std::cerr,
                                "library stats: --json takes no --queue or --playlist");
                return 1;
            }
            if (libStatsMostPlayed < -1) {
                caudio::println(std::cerr, "library stats: --most-played needs N >= 0");
                return 1;
            }
            if (detailed) {
                caudio::ipc::Command cmd{
                    caudio::ipc::LibraryStatsDetailed{libStatsMostPlayed}};
                auto res = sendRaw(cmd);
                if (!res)
                    return printErr(res.error());
                if (libStatsJson)
                    return printJson(*res);
                caudio::client::OutputFormatter fmt{false};
                fmt.print(*res, std::cout);
            } else {
                caudio::ipc::Command cmd{caudio::ipc::LibraryStats{}};
                auto res = sendRaw(cmd);
                if (!res)
                    return printErr(res.error());
                if (libStatsJson)
                    return printJson(*res);
                caudio::client::OutputFormatter fmt{false};
                fmt.print(*res, std::cout);
            }
            if (!libStatsQueues.empty() || !libStatsPlaylists.empty())
                caudio::println("");
            if (!libStatsQueues.empty()) {
                caudio::ipc::Command cmd{caudio::ipc::QueueQueues{}};
                auto res = sendRaw(cmd);
                if (!res)
                    return printErr(res.error());
                auto* qs = std::get_if<caudio::ipc::Queues>(&*res);
                if (!qs) {
                    caudio::client::OutputFormatter fmt{false};
                    fmt.print(*res, std::cout);
                    return 0;
                }
                bool all = false;
                for (auto& q : libStatsQueues) {
                    if (q == "all") {
                        all = true;
                        break;
                    }
                }
                if (all) {
                    caudio::client::OutputFormatter fmt{false};
                    fmt.print(*res, std::cout);
                } else {
                    for (auto& q : libStatsQueues) {
                        long long id = 0;
                        try {
                            id = std::stoll(q);
                        } catch (...) {
                            caudio::println(std::cerr,
                                            "library stats: bad queue selector '{}'", q);
                            return 1;
                        }
                        bool found = false;
                        for (auto& e : qs->entries) {
                            if (e.id == id) {
                                caudio::println("Queue {} '{}': {} tracks", e.id, e.name,
                                                e.tracks);
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            caudio::println(std::cerr, "library stats: no such queue: {}", id);
                            return 1;
                        }
                    }
                }
            }
            for (auto pid : libStatsPlaylists) {
                caudio::ipc::Command cmd{caudio::ipc::PlaylistTracks{pid}};
                auto res = sendRaw(cmd);
                if (!res)
                    return printErr(res.error());
                auto* pd = std::get_if<caudio::ipc::PlaylistData>(&*res);
                if (!pd) {
                    caudio::client::OutputFormatter fmt{false};
                    fmt.print(*res, std::cout);
                    continue;
                }
                std::string name;
                {
                    caudio::ipc::Command lcmd{caudio::ipc::PlaylistList{}};
                    auto lres = sendRaw(lcmd);
                    if (lres) {
                        if (auto* pl = std::get_if<caudio::ipc::Playlists>(&*lres)) {
                            for (auto& p : pl->playlists) {
                                if (p.id == pid) {
                                    name = p.name;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (name.empty()) {
                    caudio::println(std::cerr, "library stats: no such playlist: {}", pid);
                    return 1;
                }
                if (pd->tracks.size() == 1)
                    caudio::println("Playlist '{}': 1 track", name);
                else
                    caudio::println("Playlist '{}': {} tracks", name, pd->tracks.size());
            }
            return 0;
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
            return confirm(sendRaw(cmd), libAddJson,
                           std::format("Added {} to library", libAddPath));
        }
        if (libRemove->parsed()) {
            // Resolve a label first so the confirmation names the track.
            std::string label;
            bool numeric = !libRemoveQuery.empty();
            for (char c : libRemoveQuery) {
                if (!std::isdigit((unsigned char)c)) {
                    numeric = false;
                    break;
                }
            }
            if (numeric) {
                long long id = 0;
                try {
                    id = std::stoll(libRemoveQuery);
                } catch (...) {
                    id = 0;
                }
                if (id != 0) {
                    caudio::ipc::Command cmd{caudio::ipc::TagGet{id}};
                    if (auto res = sendRaw(cmd)) {
                        if (auto* st = std::get_if<caudio::ipc::SingleTrack>(&*res))
                            label = detail::addedLabel(st->track);
                    }
                }
            }
            if (label.empty()) {
                std::string fn =
                    std::filesystem::path(libRemoveQuery).filename().generic_string();
                if (!fn.empty())
                    label = fn;
            }
            caudio::ipc::Command cmd{caudio::ipc::LibraryRemove{libRemoveQuery}};
            auto res = sendRaw(cmd);
            if (!res)
                return printErr(res.error());
            if (libRemoveJson)
                return printJson(*res);
            if (!label.empty())
                caudio::println("Removed from library {}", label);
            else
                caudio::println("Removed {} from library", libRemoveQuery);
            return 0;
        }
        std::cout << libCmd->help() << "\n";
        return 0;
    }
    if (tagCmd->parsed()) {
        if (tagEdit->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::TagEdit{tagEditId, tagEditField, tagEditValue}};
            return confirm(sendRaw(cmd), tagEditJson,
                           std::format("Updated {} for track {}", tagEditField, tagEditId));
        }
        if (tagGet->parsed()) {
            static const std::array<std::string_view, 8> tagFields{
                "title", "artist", "album", "album_artist",
                "genre", "year", "track_number", "disc_number"};
            if (!tagGetField.empty() &&
                std::find(tagFields.begin(), tagFields.end(), tagGetField) ==
                    tagFields.end()) {
                caudio::println(std::cerr, "tag get: unknown field '{}' (expected one of "
                                           "title|artist|album|album_artist|genre|year|"
                                           "track_number|disc_number)",
                                tagGetField);
                return 1;
            }
            caudio::ipc::Command cmd{caudio::ipc::TagGet{tagGetId}};
            auto res = sendRaw(cmd);
            if (!res)
                return printErr(res.error());
            if (tagGetField.empty()) {
                if (tagGetJson)
                    return printJson(*res);
                caudio::client::OutputFormatter fmt{false};
                fmt.print(*res, std::cout);
                return 0;
            }
            auto* st = std::get_if<caudio::ipc::SingleTrack>(&*res);
            if (!st) {
                caudio::client::OutputFormatter fmt{tagGetJson};
                fmt.print(*res, std::cout);
                return 0;
            }
            const auto& t = st->track;
            bool numeric = false;
            std::string value;
            if (tagGetField == "title")
                value = t.title;
            else if (tagGetField == "artist")
                value = t.artist;
            else if (tagGetField == "album")
                value = t.album;
            else if (tagGetField == "album_artist")
                value = t.album_artist;
            else if (tagGetField == "genre")
                value = t.genre;
            else if (tagGetField == "year") {
                numeric = true;
                value = std::to_string(t.year);
            } else if (tagGetField == "track_number") {
                numeric = true;
                value = std::to_string(t.track_num);
            } else if (tagGetField == "disc_number") {
                numeric = true;
                value = std::to_string(t.disc_num);
            }
            if (tagGetJson) {
                caudio::utils::Json j = caudio::utils::Json::object();
                if (numeric) {
                    try {
                        j[tagGetField] = std::stoll(value);
                    } catch (...) {
                        j[tagGetField] = value;
                    }
                } else {
                    j[tagGetField] = value;
                }
                caudio::println("{}", j.dump());
                return 0;
            }
            caudio::println("{}", value);
            return 0;
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
            return confirm(sendRaw(cmd), historyClearJson, "History cleared");
        }
        std::cout << historyCmd->help() << "\n";
        return 0;
    }
    if (previewCmd->parsed())
        return handlePreview(previewFile);
    if (cfgCmd->parsed()) {
        if (cfgGet->parsed()) {
            // Answered from the local file: no daemon needed.
            auto v = caudio::config::configGetRaw(config_.configPath, cfgGetKey);
            if (!v)
                return printErr(v.error());
            caudio::ipc::Result r{caudio::ipc::ConfigValue{cfgGetKey, *v}};
            if (cfgGetJson)
                return printJson(r);
            caudio::client::OutputFormatter fmt{false};
            fmt.print(r, std::cout);
            return 0;
        }
        if (cfgSet->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::ConfigSet{cfgSetKey, cfgSetVal}};
            auto res = sendRaw(cmd);
            if (!res)
                return printErr(res.error());
            return 0;
        }
        if (cfgList->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::ConfigList{}};
            return sendViaClient(cmd, cfgListJson);
        }
        if (cfgExport->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::ConfigExport{cfgExportPath}};
            return confirm(sendRaw(cmd), cfgExportJson,
                           std::format("Exported config to {}", cfgExportPath));
        }
        if (cfgImport->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::ConfigImport{cfgImportPath}};
            return confirm(sendRaw(cmd), cfgImportJson,
                           std::format("Imported config from {}", cfgImportPath));
        }
        if (cfgReset->parsed()) {
            std::optional<std::string> k;
            if (cfgReset->count("key") > 0 && !cfgResetKey.empty())
                k = cfgResetKey;
            else if (cfgReset->count("key") > 0 && cfgResetKey.empty()) {
                // explicit empty string passed -> treat as error
                caudio::println(std::cerr, "config reset: empty key (omit --key to reset all)");
                return 1;
            }
            caudio::ipc::Command cmd{caudio::ipc::ConfigReset{k}};
            std::string line =
                k.has_value() ? std::format("Reset config key '{}'", *k) : "Reset all config";
            return confirm(sendRaw(cmd), cfgResetJson, line);
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
            return confirm(sendRaw(cmd), devSetJson, std::format("Default device: {}", devSetId));
        }
        if (devTest->parsed()) {
            std::optional<std::string> id;
            if (!devTestId.empty())
                id = devTestId;
            caudio::ipc::Command cmd{caudio::ipc::DeviceTest{id}};
            std::string line = id.has_value() ? std::format("Device available: {}", *id)
                                              : "Default device available";
            return confirm(sendRaw(cmd), devTestJson, line);
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

} // namespace caudio::app::cli
