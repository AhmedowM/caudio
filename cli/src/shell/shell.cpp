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

// Transitional: app-internal reporting helpers for not-yet-moved groups
// (playlist tracks). Frontends must use the public caudio/app/* headers.
#include <app/detail.hpp>

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

std::string statusLine(const caudio::ipc::Status& st) {
    std::string s{caudio::ipc::detail::playbackStateToString(st.state)};
    std::string who;
    if (!st.artist.empty() && !st.title.empty())
        who = st.artist + " - " + st.title;
    else
        who = st.artist + st.title;
    if (!who.empty())
        s += " " + who;
    s += " [" + caudio::app::fmtClock(st.pos) + "/" + caudio::app::fmtClock(st.dur) + "]";
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
            caudio::app::expandAddToken(tok, false, files, unmatched);
        bool hardFail = false;
        for (auto& u : unmatched) {
            if (caudio::app::hasGlobChars(u)) {
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
        return app_.playFiles(files, posSave);
    }
    if (startCmd->parsed())
        return app_.startDaemon(fg || globalFg);
    if (shutdownCmd->parsed())
        return app_.shutdownDaemon();
    // Transport commands print one-line confirmations (no ids, no queue
    // positions -- see `info` / `queue list` for those). JSON dumps raw.
    if (playCmd->parsed())
        return app_.doPlay(playJson);
    if (pauseCmd->parsed())
        return app_.pause(pauseJson);
    if (resumeCmd->parsed())
        return app_.resume(resumeJson);
    if (restartCmd->parsed())
        return app_.restart(restartJson);
    if (stopCmd->parsed())
        return app_.stop(stopJson);
    if (nextCmd->parsed())
        return app_.next(nextJson);
    if (prevCmd->parsed())
        return app_.prev(prevJson);
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
        return app_.seek(target, isRelative, seekJson);
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
        return app_.sendViaClient(cmd, jsonFlag);
    }
    if (volumeCmd->parsed()) {
        auto pv = detail::parseVolume(volumeArg);
        if (!pv) {
            caudio::println(std::cerr, "volume: '{}': {} (expected 0-100, +N/-N, mute, or unmute)",
                            volumeArg, pv.error().message);
            return 1;
        }
        caudio::ipc::Command cmd{*pv};
        return app_.sendViaClient(cmd, volumeJson);
    }
    if (queueCmd->parsed()) {
        if (qList->parsed() || qQueuesAlias->parsed())
            return app_.queues(qList->parsed() ? qJson : qQueuesJson);
        if (qTracks->parsed())
            return app_.queueTracks(qTracksOrder, qTracksJson);
        if (qSwitch->parsed())
            return app_.queueSwitch(qSwitchId, qSwitchJson);
        if (qCreate->parsed())
            return app_.queueCreate(qCreateName, qCreateJson);
        if (qDelete->parsed())
            return app_.queueDelete(qDeleteQid, qDeleteJson);
        if (qAdd->parsed())
            return app_.queueAdd(qAddPaths, qAddId, qAddSearch, qAddPlaylist, qAddReplace,
                                 qAddRecursive, qAddJson);
        if (qRemove->parsed())
            return app_.queueRemove(qRemoveId, qRemovePos, qRemovePaths, qRemoveRecursive,
                                    qRemoveJson);
        if (qMove->parsed())
            return app_.queueMove(qFrom, qTo, qMoveJson);
        if (qClear->parsed())
            return app_.queueClear(qClearJson);
        if (qShuffle->parsed())
            return app_.queueShuffle(qShuffleArg, qShuffleJson);
        if (qRepeat->parsed())
            return app_.queueRepeat(qRepeatArg, qRepeatJson);
        std::cout << queueCmd->help() << "\n";
        return 0;
    }
    if (plCmd->parsed()) {
        if (plList->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::PlaylistList{}};
            return app_.sendViaClient(cmd, plJson);
        }
        if (plTracks->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::PlaylistTracks{plTracksPid}};
            return app_.sendViaClient(cmd, plTracksJson);
        }
        if (plCreate->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::PlaylistCreate{plCreateName}};
            auto res = app_.sendRaw(cmd);
            if (!res)
                return app_.printErr(res.error());
            if (plCreateJson)
                return app_.printJson(*res);
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
                if (!caudio::app::isNumeric(s)) {
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
                caudio::app::expandAddToken(tok, plAddRecursive, files, unmatched);
            bool hardFail = false;
            for (auto& u : unmatched) {
                if (caudio::app::isNumeric(u)) {
                    caudio::println(std::cerr,
                                    "playlist add: '{}' is not a file (use --id for ids)", u);
                    hardFail = true;
                } else if (caudio::app::hasGlobChars(u)) {
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
                auto ares = app_.sendRaw(acmd);
                if (!ares) {
                    app_.printErr(ares.error());
                    hardFail = true;
                    continue;
                }
                std::string fn = std::filesystem::path(f).filename().generic_string();
                caudio::ipc::Command scmd{caudio::ipc::LibrarySearch{fn, 50}};
                auto sres = app_.sendRaw(scmd);
                if (!sres) {
                    app_.printErr(sres.error());
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
                    if (caudio::app::pathKey(t.path) == caudio::app::pathKey(f)) {
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
                auto res = app_.sendRaw(cmd);
                if (!res) {
                    const auto& e = res.error();
                    if (e.code == caudio::utils::StatusCode::AlreadyExists) {
                        caudio::println(std::cerr, "already on playlist: track {}", tid);
                        continue;
                    }
                    app_.printErr(e);
                    hardFail = true;
                    continue;
                }
                if (plAddJson) {
                    caudio::ipc::Command gcmd{caudio::ipc::TagGet{tid}};
                    if (auto gres = app_.sendRaw(gcmd)) {
                        if (auto* st = std::get_if<caudio::ipc::SingleTrack>(&*gres))
                            collected.tracks.push_back(st->track);
                    }
                } else {
                    std::string label;
                    caudio::ipc::Command gcmd{caudio::ipc::TagGet{tid}};
                    if (auto gres = app_.sendRaw(gcmd)) {
                        if (auto* st = std::get_if<caudio::ipc::SingleTrack>(&*gres))
                            label = caudio::app::addedLabel(st->track);
                    }
                    if (label.empty())
                        label = "track " + std::to_string(tid);
                    caudio::println("Added {} to playlist {}", label, plAddPid);
                }
                ++added;
            }
            if (plAddJson)
                return app_.printJson(caudio::ipc::Result{std::move(collected)});
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
            auto res = app_.sendRaw(cmd);
            if (!res)
                return app_.printErr(res.error());
            if (plLoadJson)
                return app_.printJson(*res);
            if (auto* pl = std::get_if<caudio::ipc::PlaylistLoaded>(&*res)) {
                if (plLoadReplace)
                    caudio::println("Replaced queue {} with playlist {}", pl->queue_id,
                                    plLoadPid);
                else
                    caudio::println("Loaded playlist {} into queue {}", plLoadPid,
                                    pl->queue_id);
                if (plLoadPlay)
                    caudio::println("Playing {}", caudio::app::trackWho(pl->status));
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
            return app_.confirm(app_.sendRaw(cmd), plSaveJson,
                           std::format("Saved playlist '{}'", plSaveName));
        }
        if (plDelete->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::PlaylistDelete{plDeletePid}};
            return app_.confirm(app_.sendRaw(cmd), plDeleteJson,
                           std::format("Deleted playlist {}", plDeletePid));
        }
        if (plRename->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::PlaylistRename{plRenamePid, plRenameName}};
            return app_.confirm(app_.sendRaw(cmd), plRenameJson,
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
            return app_.sendViaClient(cmd, plImportJson);
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
            return app_.sendViaClient(cmd, libScanJson);
        }
        if (libSearch->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::LibrarySearch{libSearchQuery, libSearchLimit}};
            auto res = app_.sendRaw(cmd);
            if (!res)
                return app_.printErr(res.error());
            if (libSearchJson)
                return app_.printJson(*res);
            caudio::client::OutputFormatter fmt{false, caudio::app::detail::useColor()};
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
                auto res = app_.sendRaw(cmd);
                if (!res)
                    return app_.printErr(res.error());
                if (libStatsJson)
                    return app_.printJson(*res);
                caudio::client::OutputFormatter fmt{false};
                fmt.print(*res, std::cout);
            } else {
                caudio::ipc::Command cmd{caudio::ipc::LibraryStats{}};
                auto res = app_.sendRaw(cmd);
                if (!res)
                    return app_.printErr(res.error());
                if (libStatsJson)
                    return app_.printJson(*res);
                caudio::client::OutputFormatter fmt{false};
                fmt.print(*res, std::cout);
            }
            if (!libStatsQueues.empty() || !libStatsPlaylists.empty())
                caudio::println("");
            if (!libStatsQueues.empty()) {
                caudio::ipc::Command cmd{caudio::ipc::QueueQueues{}};
                auto res = app_.sendRaw(cmd);
                if (!res)
                    return app_.printErr(res.error());
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
                auto res = app_.sendRaw(cmd);
                if (!res)
                    return app_.printErr(res.error());
                auto* pd = std::get_if<caudio::ipc::PlaylistData>(&*res);
                if (!pd) {
                    caudio::client::OutputFormatter fmt{false};
                    fmt.print(*res, std::cout);
                    continue;
                }
                std::string name;
                {
                    caudio::ipc::Command lcmd{caudio::ipc::PlaylistList{}};
                    auto lres = app_.sendRaw(lcmd);
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
            return app_.sendViaClient(cmd, libListJson);
        }
        if (libAdd->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::LibraryAdd{libAddPath, libAddRecursive}};
            return app_.confirm(app_.sendRaw(cmd), libAddJson,
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
                    if (auto res = app_.sendRaw(cmd)) {
                        if (auto* st = std::get_if<caudio::ipc::SingleTrack>(&*res))
                            label = caudio::app::addedLabel(st->track);
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
            auto res = app_.sendRaw(cmd);
            if (!res)
                return app_.printErr(res.error());
            if (libRemoveJson)
                return app_.printJson(*res);
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
            return app_.confirm(app_.sendRaw(cmd), tagEditJson,
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
            auto res = app_.sendRaw(cmd);
            if (!res)
                return app_.printErr(res.error());
            if (tagGetField.empty()) {
                if (tagGetJson)
                    return app_.printJson(*res);
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
            return app_.sendViaClient(cmd, historyJson);
        }
        if (historyClear->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::HistoryClear{}};
            return app_.confirm(app_.sendRaw(cmd), historyClearJson, "History cleared");
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
                return app_.printErr(v.error());
            caudio::ipc::Result r{caudio::ipc::ConfigValue{cfgGetKey, *v}};
            if (cfgGetJson)
                return app_.printJson(r);
            caudio::client::OutputFormatter fmt{false};
            fmt.print(r, std::cout);
            return 0;
        }
        if (cfgSet->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::ConfigSet{cfgSetKey, cfgSetVal}};
            auto res = app_.sendRaw(cmd);
            if (!res)
                return app_.printErr(res.error());
            return 0;
        }
        if (cfgList->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::ConfigList{}};
            return app_.sendViaClient(cmd, cfgListJson);
        }
        if (cfgExport->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::ConfigExport{cfgExportPath}};
            return app_.confirm(app_.sendRaw(cmd), cfgExportJson,
                           std::format("Exported config to {}", cfgExportPath));
        }
        if (cfgImport->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::ConfigImport{cfgImportPath}};
            return app_.confirm(app_.sendRaw(cmd), cfgImportJson,
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
            return app_.confirm(app_.sendRaw(cmd), cfgResetJson, line);
        }
        std::cout << cfgCmd->help() << "\n";
        return 0;
    }
    if (deviceCmd->parsed()) {
        if (devList->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::DeviceList{}};
            return app_.sendViaClient(cmd, devJson);
        }
        if (devSet->parsed()) {
            caudio::ipc::Command cmd{caudio::ipc::DeviceSet{devSetId}};
            return app_.confirm(app_.sendRaw(cmd), devSetJson, std::format("Default device: {}", devSetId));
        }
        if (devTest->parsed()) {
            std::optional<std::string> id;
            if (!devTestId.empty())
                id = devTestId;
            caudio::ipc::Command cmd{caudio::ipc::DeviceTest{id}};
            std::string line = id.has_value() ? std::format("Device available: {}", *id)
                                              : "Default device available";
            return app_.confirm(app_.sendRaw(cmd), devTestJson, line);
        }
        std::cout << deviceCmd->help() << "\n";
        return 0;
    }
    if (infoCmd->parsed()) {
        caudio::ipc::Command cmd{caudio::ipc::Info{}};
        return app_.sendViaClient(cmd, infoJson);
    }
    std::cout << cli_->help() << "\n";
    return 0;
}

} // namespace caudio::app::cli
