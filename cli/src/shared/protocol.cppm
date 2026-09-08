module;
#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module caudio.cli:protocol;

import caudio.utils;
import caudio.engine;
import caudio.db;
import :command;
import :result;

export namespace caudio::cli {

using ordered_json = nlohmann::ordered_json;

struct IpcRequest final {
    uint32_t id{0};
    Command cmd{};
};

struct IpcReply final {
    uint32_t id{0};
    std::expected<Result, caudio::utils::Error> result{};
};

// ---------------------------------------------------------------------------
// JSON helpers for enums
// ---------------------------------------------------------------------------
namespace detail {

inline std::string playbackStateToString(caudio::engine::PlaybackState s) {
    using PS = caudio::engine::PlaybackState;
    switch (s) {
    case PS::Stopped: return "Stopped";
    case PS::Ready: return "Ready";
    case PS::Playing: return "Playing";
    case PS::Paused: return "Paused";
    default: return "Unknown";
    }
}

inline std::expected<caudio::engine::PlaybackState, caudio::utils::Error>
playbackStateFromString(std::string_view sv) {
    using PS = caudio::engine::PlaybackState;
    if (sv == "Stopped") return PS::Stopped;
    if (sv == "Ready") return PS::Ready;
    if (sv == "Playing") return PS::Playing;
    if (sv == "Paused") return PS::Paused;
    return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "unknown PlaybackState")};
}

inline std::string repeatModeToString(caudio::engine::RepeatMode m) {
    using RM = caudio::engine::RepeatMode;
    switch (m) {
    case RM::Off: return "Off";
    case RM::Queue: return "Queue";
    case RM::One: return "One";
    default: return "Unknown";
    }
}

inline std::expected<caudio::engine::RepeatMode, caudio::utils::Error>
repeatModeFromString(std::string_view sv) {
    using RM = caudio::engine::RepeatMode;
    if (sv == "Off") return RM::Off;
    if (sv == "Queue") return RM::Queue;
    if (sv == "One") return RM::One;
    return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "unknown RepeatMode")};
}

inline std::string resultCodeToString(caudio::utils::Result r) {
    return std::string(caudio::utils::toString(r));
}

inline std::expected<caudio::utils::Result, caudio::utils::Error>
resultCodeFromString(std::string_view sv) {
    using R = caudio::utils::Result;
    if (sv == "Ok") return R::Ok;
    if (sv == "InvalidArg") return R::InvalidArg;
    if (sv == "NotFound") return R::NotFound;
    if (sv == "Unsupported") return R::Unsupported;
    if (sv == "Io") return R::Io;
    if (sv == "Device") return R::Device;
    if (sv == "State") return R::State;
    if (sv == "NoMem") return R::NoMem;
    if (sv == "Internal") return R::Internal;
    if (sv == "AlreadyExists") return R::AlreadyExists;
    if (sv == "Busy") return R::Busy;
    if (sv == "Corrupt") return R::Corrupt;
    if (sv == "NoSpace") return R::NoSpace;
    return std::unexpected{caudio::utils::makeError(R::InvalidArg, "unknown Result code")};
}

// Track JSON helpers
inline ordered_json trackToJson(const caudio::db::Track& t) {
    ordered_json j;
    j["id"] = t.id;
    j["path"] = t.path;
    j["title"] = t.title;
    j["artist"] = t.artist;
    j["album"] = t.album;
    j["duration"] = t.duration;
    j["sample_rate"] = t.sample_rate;
    j["channels"] = t.channels;
    j["bitrate"] = t.bitrate;
    j["year"] = t.year;
    j["track_num"] = t.track_num;
    j["genre"] = t.genre;
    return j;
}

inline std::expected<caudio::db::Track, caudio::utils::Error>
trackFromJson(const ordered_json& j) {
    try {
        caudio::db::Track t{};
        if (j.contains("id") && j["id"].is_number()) t.id = j["id"].get<int64_t>();
        if (j.contains("path") && j["path"].is_string()) t.path = j["path"].get<std::string>();
        if (j.contains("title") && j["title"].is_string()) t.title = j["title"].get<std::string>();
        if (j.contains("artist") && j["artist"].is_string()) t.artist = j["artist"].get<std::string>();
        if (j.contains("album") && j["album"].is_string()) t.album = j["album"].get<std::string>();
        if (j.contains("duration") && j["duration"].is_number()) t.duration = j["duration"].get<double>();
        if (j.contains("sample_rate") && j["sample_rate"].is_number()) t.sample_rate = j["sample_rate"].get<uint32_t>();
        if (j.contains("channels") && j["channels"].is_number()) t.channels = j["channels"].get<uint32_t>();
        if (j.contains("bitrate") && j["bitrate"].is_number()) t.bitrate = j["bitrate"].get<int32_t>();
        if (j.contains("year") && j["year"].is_number()) t.year = j["year"].get<int32_t>();
        if (j.contains("track_num") && j["track_num"].is_number()) t.track_num = j["track_num"].get<int32_t>();
        if (j.contains("genre") && j["genre"].is_string()) t.genre = j["genre"].get<std::string>();
        return t;
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, e.what())};
    }
}

inline ordered_json errorToJson(const caudio::utils::Error& e) {
    ordered_json j;
    j["type"] = "Error";
    j["code"] = resultCodeToString(e.code);
    j["code_value"] = std::to_underlying(e.code);
    j["message"] = e.message;
    return j;
}

inline std::expected<caudio::utils::Error, caudio::utils::Error>
errorFromJson(const ordered_json& j) {
    try {
        std::string codeStr;
        std::string msg;
        if (j.contains("code") && j["code"].is_string()) codeStr = j["code"].get<std::string>();
        else if (j.contains("code_value") && j["code_value"].is_number()) {
            int v = j["code_value"].get<int>();
            // map via to_underlying comparison
            for (auto c : {caudio::utils::Result::Ok, caudio::utils::Result::InvalidArg, caudio::utils::Result::NotFound,
                           caudio::utils::Result::Unsupported, caudio::utils::Result::Io, caudio::utils::Result::Device,
                           caudio::utils::Result::State, caudio::utils::Result::NoMem, caudio::utils::Result::Internal,
                           caudio::utils::Result::AlreadyExists, caudio::utils::Result::Busy, caudio::utils::Result::Corrupt,
                           caudio::utils::Result::NoSpace}) {
                if (std::to_underlying(c) == v) {
                    codeStr = std::string(caudio::utils::toString(c));
                    break;
                }
            }
        }
        if (j.contains("message") && j["message"].is_string()) msg = j["message"].get<std::string>();
        if (codeStr.empty()) codeStr = "Internal";
        auto rc = resultCodeFromString(codeStr);
        if (!rc) return std::unexpected{rc.error()};
        return caudio::utils::Error{*rc, msg};
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, e.what())};
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// Command JSON
// ---------------------------------------------------------------------------
ordered_json toJson(const Command& cmd) {
    return std::visit([](const auto& v) -> ordered_json {
        using T = std::decay_t<decltype(v)>;
        ordered_json j;
        if constexpr (std::is_same_v<T, Play>) {
            j["type"] = "Play";
        } else if constexpr (std::is_same_v<T, Pause>) {
            j["type"] = "Pause";
        } else if constexpr (std::is_same_v<T, Resume>) {
            j["type"] = "Resume";
        } else if constexpr (std::is_same_v<T, Restart>) {
            j["type"] = "Restart";
        } else if constexpr (std::is_same_v<T, Stop>) {
            j["type"] = "Stop";
        } else if constexpr (std::is_same_v<T, Next>) {
            j["type"] = "Next";
        } else if constexpr (std::is_same_v<T, Prev>) {
            j["type"] = "Prev";
        } else if constexpr (std::is_same_v<T, Seek>) {
            j["type"] = "Seek";
            j["seconds"] = v.seconds;
        } else if constexpr (std::is_same_v<T, StatusReq>) {
            j["type"] = "StatusReq";
        } else if constexpr (std::is_same_v<T, VolumeSet>) {
            j["type"] = "VolumeSet";
            if (v.level.has_value()) j["level"] = *v.level;
            else j["level"] = nullptr;
            if (v.mute.has_value()) j["mute"] = *v.mute;
            else j["mute"] = nullptr;
            if (v.deltaPct.has_value()) j["deltaPct"] = *v.deltaPct;
            else j["deltaPct"] = nullptr;
        } else if constexpr (std::is_same_v<T, QueueList>) {
            j["type"] = "QueueList";
        } else if constexpr (std::is_same_v<T, QueueQueues>) {
            j["type"] = "QueueQueues";
        } else if constexpr (std::is_same_v<T, QueueSwitch>) {
            j["type"] = "QueueSwitch";
            j["qid"] = v.qid;
        } else if constexpr (std::is_same_v<T, QueueAdd>) {
            j["type"] = "QueueAdd";
            j["query"] = v.query;
            j["search"] = v.search;
        } else if constexpr (std::is_same_v<T, QueueRemove>) {
            j["type"] = "QueueRemove";
            j["idOrIndex"] = v.idOrIndex;
        } else if constexpr (std::is_same_v<T, QueueMove>) {
            j["type"] = "QueueMove";
            j["from"] = v.from;
            j["to"] = v.to;
        } else if constexpr (std::is_same_v<T, QueueClear>) {
            j["type"] = "QueueClear";
        } else if constexpr (std::is_same_v<T, QueueShuffle>) {
            j["type"] = "QueueShuffle";
            if (v.on.has_value()) j["on"] = *v.on;
            else j["on"] = nullptr;
        } else if constexpr (std::is_same_v<T, QueueRepeat>) {
            j["type"] = "QueueRepeat";
            if (v.mode.has_value()) j["mode"] = detail::repeatModeToString(*v.mode);
            else j["mode"] = nullptr;
        } else if constexpr (std::is_same_v<T, PlaylistList>) {
            j["type"] = "PlaylistList";
        } else if constexpr (std::is_same_v<T, PlaylistTracks>) {
            j["type"] = "PlaylistTracks";
            j["pid"] = v.pid;
        } else if constexpr (std::is_same_v<T, PlaylistLoad>) {
            j["type"] = "PlaylistLoad";
            j["pid"] = v.pid;
            j["play"] = v.play;
        } else if constexpr (std::is_same_v<T, PlaylistSave>) {
            j["type"] = "PlaylistSave";
            j["name"] = v.name;
            if (v.queueId.has_value()) j["queueId"] = *v.queueId;
            else j["queueId"] = nullptr;
        } else if constexpr (std::is_same_v<T, PlaylistDelete>) {
            j["type"] = "PlaylistDelete";
            j["pid"] = v.pid;
        } else if constexpr (std::is_same_v<T, LibraryScan>) {
            j["type"] = "LibraryScan";
            if (v.path.has_value()) j["path"] = *v.path;
            else j["path"] = nullptr;
            j["mode"] = v.mode;
        } else if constexpr (std::is_same_v<T, LibrarySearch>) {
            j["type"] = "LibrarySearch";
            j["query"] = v.query;
            j["limit"] = v.limit;
        } else if constexpr (std::is_same_v<T, LibraryStats>) {
            j["type"] = "LibraryStats";
        } else if constexpr (std::is_same_v<T, ConfigGet>) {
            j["type"] = "ConfigGet";
            j["key"] = v.key;
        } else if constexpr (std::is_same_v<T, ConfigSet>) {
            j["type"] = "ConfigSet";
            j["key"] = v.key;
            j["value"] = v.value;
        } else if constexpr (std::is_same_v<T, ConfigList>) {
            j["type"] = "ConfigList";
        } else if constexpr (std::is_same_v<T, ConfigExport>) {
            j["type"] = "ConfigExport";
            j["path"] = v.path;
        } else if constexpr (std::is_same_v<T, ConfigImport>) {
            j["type"] = "ConfigImport";
            j["path"] = v.path;
        } else if constexpr (std::is_same_v<T, Shutdown>) {
            j["type"] = "Shutdown";
        } else if constexpr (std::is_same_v<T, Preview>) {
            j["type"] = "Preview";
            j["file"] = v.file;
        }
        return j;
    }, cmd);
}

std::expected<Command, caudio::utils::Error> commandFromJson(const ordered_json& j) {
    try {
        if (!j.contains("type") || !j["type"].is_string()) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "missing type")};
        }
        std::string t = j["type"].get<std::string>();
        if (t == "Play") return Command{Play{}};
        if (t == "Pause") return Command{Pause{}};
        if (t == "Resume") return Command{Resume{}};
        if (t == "Restart") return Command{Restart{}};
        if (t == "Stop") return Command{Stop{}};
        if (t == "Next") return Command{Next{}};
        if (t == "Prev") return Command{Prev{}};
        if (t == "Seek") {
            double sec = 0;
            if (j.contains("seconds") && j["seconds"].is_number()) sec = j["seconds"].get<double>();
            return Command{Seek{sec}};
        }
        if (t == "StatusReq") return Command{StatusReq{}};
        if (t == "VolumeSet") {
            VolumeSet v{};
            if (j.contains("level") && !j["level"].is_null() && j["level"].is_number()) v.level = j["level"].get<float>();
            if (j.contains("mute") && !j["mute"].is_null() && j["mute"].is_boolean()) v.mute = j["mute"].get<bool>();
            if (j.contains("deltaPct") && !j["deltaPct"].is_null() && j["deltaPct"].is_number()) v.deltaPct = j["deltaPct"].get<int>();
            return Command{std::move(v)};
        }
        if (t == "QueueList") return Command{QueueList{}};
        if (t == "QueueQueues") return Command{QueueQueues{}};
        if (t == "QueueSwitch") {
            int64_t qid = 1;
            if (j.contains("qid") && j["qid"].is_number()) qid = j["qid"].get<int64_t>();
            return Command{QueueSwitch{qid}};
        }
        if (t == "QueueAdd") {
            std::string q;
            bool search = false;
            if (j.contains("query") && j["query"].is_string()) q = j["query"].get<std::string>();
            if (j.contains("search") && j["search"].is_boolean()) search = j["search"].get<bool>();
            return Command{QueueAdd{std::move(q), search}};
        }
        if (t == "QueueRemove") {
            std::string id;
            if (j.contains("idOrIndex") && j["idOrIndex"].is_string()) id = j["idOrIndex"].get<std::string>();
            return Command{QueueRemove{std::move(id)}};
        }
        if (t == "QueueMove") {
            std::size_t from = 0, to = 0;
            if (j.contains("from") && j["from"].is_number()) from = j["from"].get<std::size_t>();
            if (j.contains("to") && j["to"].is_number()) to = j["to"].get<std::size_t>();
            return Command{QueueMove{from, to}};
        }
        if (t == "QueueClear") return Command{QueueClear{}};
        if (t == "QueueShuffle") {
            QueueShuffle v{};
            if (j.contains("on") && !j["on"].is_null() && j["on"].is_boolean()) v.on = j["on"].get<bool>();
            return Command{std::move(v)};
        }
        if (t == "QueueRepeat") {
            QueueRepeat v{};
            if (j.contains("mode") && !j["mode"].is_null() && j["mode"].is_string()) {
                auto m = detail::repeatModeFromString(j["mode"].get<std::string>());
                if (!m) return std::unexpected{m.error()};
                v.mode = *m;
            }
            return Command{std::move(v)};
        }
        if (t == "PlaylistList") return Command{PlaylistList{}};
        if (t == "PlaylistTracks") {
            int64_t pid = 0;
            if (j.contains("pid") && j["pid"].is_number()) pid = j["pid"].get<int64_t>();
            return Command{PlaylistTracks{pid}};
        }
        if (t == "PlaylistLoad") {
            int64_t pid = 0;
            bool play = false;
            if (j.contains("pid") && j["pid"].is_number()) pid = j["pid"].get<int64_t>();
            if (j.contains("play") && j["play"].is_boolean()) play = j["play"].get<bool>();
            return Command{PlaylistLoad{pid, play}};
        }
        if (t == "PlaylistSave") {
            std::string name;
            std::optional<int64_t> qid;
            if (j.contains("name") && j["name"].is_string()) name = j["name"].get<std::string>();
            if (j.contains("queueId") && !j["queueId"].is_null() && j["queueId"].is_number()) qid = j["queueId"].get<int64_t>();
            return Command{PlaylistSave{std::move(name), qid}};
        }
        if (t == "PlaylistDelete") {
            int64_t pid = 0;
            if (j.contains("pid") && j["pid"].is_number()) pid = j["pid"].get<int64_t>();
            return Command{PlaylistDelete{pid}};
        }
        if (t == "LibraryScan") {
            LibraryScan v{};
            if (j.contains("path") && !j["path"].is_null() && j["path"].is_string()) v.path = j["path"].get<std::string>();
            if (j.contains("mode") && j["mode"].is_string()) v.mode = j["mode"].get<std::string>();
            return Command{std::move(v)};
        }
        if (t == "LibrarySearch") {
            std::string q;
            int lim = 50;
            if (j.contains("query") && j["query"].is_string()) q = j["query"].get<std::string>();
            if (j.contains("limit") && j["limit"].is_number()) lim = j["limit"].get<int>();
            return Command{LibrarySearch{std::move(q), lim}};
        }
        if (t == "LibraryStats") return Command{LibraryStats{}};
        if (t == "ConfigGet") {
            std::string k;
            if (j.contains("key") && j["key"].is_string()) k = j["key"].get<std::string>();
            return Command{ConfigGet{std::move(k)}};
        }
        if (t == "ConfigSet") {
            std::string k, v;
            if (j.contains("key") && j["key"].is_string()) k = j["key"].get<std::string>();
            if (j.contains("value") && j["value"].is_string()) v = j["value"].get<std::string>();
            return Command{ConfigSet{std::move(k), std::move(v)}};
        }
        if (t == "ConfigList") return Command{ConfigList{}};
        if (t == "ConfigExport") {
            std::string p;
            if (j.contains("path") && j["path"].is_string()) p = j["path"].get<std::string>();
            return Command{ConfigExport{std::move(p)}};
        }
        if (t == "ConfigImport") {
            std::string p;
            if (j.contains("path") && j["path"].is_string()) p = j["path"].get<std::string>();
            return Command{ConfigImport{std::move(p)}};
        }
        if (t == "Shutdown") return Command{Shutdown{}};
        if (t == "Preview") {
            std::string f;
            if (j.contains("file") && j["file"].is_string()) f = j["file"].get<std::string>();
            return Command{Preview{std::move(f)}};
        }
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "unknown Command type: " + t)};
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, e.what())};
    }
}

// Generic fromJson template wrapper: fromJson<Command>(json)
template <typename T>
std::expected<T, caudio::utils::Error> fromJson(const ordered_json& j) {
    if constexpr (std::is_same_v<T, Command>) {
        return commandFromJson(j);
    } else if constexpr (std::is_same_v<T, Result>) {
        // forwarded to resultFromJson declared below; use if constexpr dispatch via overload
        // This branch will be instantiated only for Result; to avoid incomplete type,
        // we handle Result via separate function resultFromJson and call it here.
        // We need forward declaration: implement after Result helpers.
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Unsupported, "use resultFromJson")};
    } else {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Unsupported, "unsupported fromJson type")};
    }
}

// ---------------------------------------------------------------------------
// Result JSON
// ---------------------------------------------------------------------------
ordered_json toJson(const Result& r) {
    return std::visit([](const auto& v) -> ordered_json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Status>) {
            ordered_json j;
            j["type"] = "Status";
            j["state"] = detail::playbackStateToString(v.state);
            j["state_value"] = std::to_underlying(v.state);
            j["pos"] = v.pos;
            j["dur"] = v.dur;
            j["vol"] = v.vol;
            j["muted"] = v.muted;
            j["shuffle"] = v.shuffle;
            j["repeat"] = detail::repeatModeToString(v.repeat);
            j["repeat_value"] = std::to_underlying(v.repeat);
            j["trackId"] = v.trackId;
            j["title"] = v.title;
            j["artist"] = v.artist;
            j["path"] = v.path;
            j["qSize"] = v.qSize;
            j["qIdx"] = v.qIdx;
            return j;
        } else if constexpr (std::is_same_v<T, QueueTracks>) {
            ordered_json j;
            j["type"] = "QueueTracks";
            j["tracks"] = ordered_json::array();
            for (const auto& t : v.tracks) j["tracks"].push_back(detail::trackToJson(t));
            return j;
        } else if constexpr (std::is_same_v<T, VolumeInfo>) {
            ordered_json j;
            j["type"] = "VolumeInfo";
            j["vol"] = v.vol;
            j["muted"] = v.muted;
            return j;
        } else if constexpr (std::is_same_v<T, LibraryStatsData>) {
            ordered_json j;
            j["type"] = "LibraryStats";
            j["tracks"] = v.tracks;
            j["queues"] = v.queues;
            j["playlists"] = v.playlists;
            return j;
        } else if constexpr (std::is_same_v<T, Tracks>) {
            ordered_json j;
            j["type"] = "Tracks";
            j["tracks"] = ordered_json::array();
            for (const auto& t : v.tracks) j["tracks"].push_back(detail::trackToJson(t));
            return j;
        } else if constexpr (std::is_same_v<T, Empty>) {
            ordered_json j;
            j["type"] = "Empty";
            return j;
        } else if constexpr (std::is_same_v<T, caudio::utils::Error>) {
            return detail::errorToJson(v);
        } else {
            ordered_json j;
            j["type"] = "Unknown";
            return j;
        }
    }, r);
}

std::expected<Result, caudio::utils::Error> resultFromJson(const ordered_json& j) {
    try {
        if (!j.contains("type") || !j["type"].is_string()) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "missing Result type")};
        }
        std::string t = j["type"].get<std::string>();
        if (t == "Status") {
            Status s{};
            if (j.contains("state") && j["state"].is_string()) {
                auto ps = detail::playbackStateFromString(j["state"].get<std::string>());
                if (!ps) return std::unexpected{ps.error()};
                s.state = *ps;
            } else if (j.contains("state_value") && j["state_value"].is_number()) {
                int v = j["state_value"].get<int>();
                s.state = static_cast<caudio::engine::PlaybackState>(v);
            }
            if (j.contains("pos") && j["pos"].is_number()) s.pos = j["pos"].get<double>();
            if (j.contains("dur") && j["dur"].is_number()) s.dur = j["dur"].get<double>();
            if (j.contains("vol") && j["vol"].is_number()) s.vol = j["vol"].get<float>();
            if (j.contains("muted") && j["muted"].is_boolean()) s.muted = j["muted"].get<bool>();
            if (j.contains("shuffle") && j["shuffle"].is_boolean()) s.shuffle = j["shuffle"].get<bool>();
            if (j.contains("repeat") && j["repeat"].is_string()) {
                auto rm = detail::repeatModeFromString(j["repeat"].get<std::string>());
                if (!rm) return std::unexpected{rm.error()};
                s.repeat = *rm;
            } else if (j.contains("repeat_value") && j["repeat_value"].is_number()) {
                int v = j["repeat_value"].get<int>();
                s.repeat = static_cast<caudio::engine::RepeatMode>(v);
            }
            if (j.contains("trackId") && j["trackId"].is_number()) s.trackId = j["trackId"].get<int64_t>();
            if (j.contains("title") && j["title"].is_string()) s.title = j["title"].get<std::string>();
            if (j.contains("artist") && j["artist"].is_string()) s.artist = j["artist"].get<std::string>();
            if (j.contains("path") && j["path"].is_string()) s.path = j["path"].get<std::string>();
            if (j.contains("qSize") && j["qSize"].is_number()) s.qSize = j["qSize"].get<std::size_t>();
            if (j.contains("qIdx") && j["qIdx"].is_number()) s.qIdx = j["qIdx"].get<std::size_t>();
            return Result{std::move(s)};
        }
        if (t == "QueueTracks") {
            QueueTracks qt{};
            if (j.contains("tracks") && j["tracks"].is_array()) {
                for (const auto& jt : j["tracks"]) {
                    auto tr = detail::trackFromJson(jt);
                    if (!tr) return std::unexpected{tr.error()};
                    qt.tracks.push_back(std::move(*tr));
                }
            }
            return Result{std::move(qt)};
        }
        if (t == "VolumeInfo") {
            VolumeInfo vi{};
            if (j.contains("vol") && j["vol"].is_number()) vi.vol = j["vol"].get<float>();
            if (j.contains("muted") && j["muted"].is_boolean()) vi.muted = j["muted"].get<bool>();
            return Result{std::move(vi)};
        }
        if (t == "LibraryStats") {
            LibraryStatsData ls{};
            if (j.contains("tracks") && j["tracks"].is_number()) ls.tracks = j["tracks"].get<std::size_t>();
            if (j.contains("queues") && j["queues"].is_number()) ls.queues = j["queues"].get<std::size_t>();
            if (j.contains("playlists") && j["playlists"].is_number()) ls.playlists = j["playlists"].get<std::size_t>();
            return Result{std::move(ls)};
        }
        if (t == "Tracks") {
            Tracks trs{};
            if (j.contains("tracks") && j["tracks"].is_array()) {
                for (const auto& jt : j["tracks"]) {
                    auto tr = detail::trackFromJson(jt);
                    if (!tr) return std::unexpected{tr.error()};
                    trs.tracks.push_back(std::move(*tr));
                }
            }
            return Result{std::move(trs)};
        }
        if (t == "Empty") {
            return Result{Empty{}};
        }
        if (t == "Error") {
            auto e = detail::errorFromJson(j);
            if (!e) return std::unexpected{e.error()};
            return Result{*e};
        }
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "unknown Result type: " + t)};
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, e.what())};
    }
}

// ---------------------------------------------------------------------------
// IpcRequest / IpcReply serialization
// ---------------------------------------------------------------------------
std::string serializeRequest(const IpcRequest& req) {
    ordered_json j;
    j["id"] = req.id;
    j["cmd"] = toJson(req.cmd);
    return j.dump();
}

std::expected<IpcRequest, caudio::utils::Error> deserializeRequest(std::string_view sv) {
    try {
        auto j = ordered_json::parse(sv);
        if (!j.contains("id") || !j.contains("cmd")) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "missing id/cmd")};
        }
        uint32_t id = j["id"].get<uint32_t>();
        auto cmd = commandFromJson(j["cmd"]);
        if (!cmd) return std::unexpected{cmd.error()};
        return IpcRequest{id, std::move(*cmd)};
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, e.what())};
    }
}

std::string serializeReply(const IpcReply& rep) {
    ordered_json j;
    j["id"] = rep.id;
    if (rep.result.has_value()) {
        j["ok"] = true;
        j["result"] = toJson(*rep.result);
    } else {
        j["ok"] = false;
        j["error"] = detail::errorToJson(rep.result.error());
    }
    return j.dump();
}

std::expected<IpcReply, caudio::utils::Error> deserializeReply(std::string_view sv) {
    try {
        auto j = ordered_json::parse(sv);
        if (!j.contains("id")) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "missing id")};
        }
        uint32_t id = j["id"].get<uint32_t>();
        bool ok = true;
        if (j.contains("ok") && j["ok"].is_boolean()) ok = j["ok"].get<bool>();
        else if (j.contains("error")) ok = false;

        if (ok) {
            if (!j.contains("result")) {
                return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "missing result")};
            }
            auto r = resultFromJson(j["result"]);
            if (!r) return std::unexpected{r.error()};
            return IpcReply{id, std::move(*r)};
        } else {
            ordered_json ej;
            if (j.contains("error")) ej = j["error"];
            else if (j.contains("result")) ej = j["result"];
            else return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "missing error")};
            auto e = detail::errorFromJson(ej);
            if (!e) return std::unexpected{e.error()};
            return IpcReply{id, std::unexpected{*e}};
        }
    } catch (const std::exception& e) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::Corrupt, e.what())};
    }
}

// ---------------------------------------------------------------------------
// Framing: [4-byte BE len][json]
// ---------------------------------------------------------------------------
std::vector<std::byte> frame(std::string_view json) {
    std::vector<std::byte> out;
    out.reserve(4 + json.size());
    uint32_t len = static_cast<uint32_t>(json.size());
    out.push_back(static_cast<std::byte>((len >> 24) & 0xFF));
    out.push_back(static_cast<std::byte>((len >> 16) & 0xFF));
    out.push_back(static_cast<std::byte>((len >> 8) & 0xFF));
    out.push_back(static_cast<std::byte>(len & 0xFF));
    for (char c : json) out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    return out;
}

std::expected<std::string, caudio::utils::Error> deframe(std::span<const std::byte> buf) {
    if (buf.size() < 4) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "frame too short")};
    }
    uint32_t len = (static_cast<uint32_t>(std::to_integer<unsigned char>(buf[0])) << 24) |
                   (static_cast<uint32_t>(std::to_integer<unsigned char>(buf[1])) << 16) |
                   (static_cast<uint32_t>(std::to_integer<unsigned char>(buf[2])) << 8) |
                   static_cast<uint32_t>(std::to_integer<unsigned char>(buf[3]));
    if (buf.size() < 4 + len) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "frame incomplete")};
    }
    std::string s;
    s.reserve(len);
    for (std::size_t i = 0; i < len; ++i) {
        s.push_back(static_cast<char>(std::to_integer<unsigned char>(buf[4 + i])));
    }
    return s;
}

inline std::string toJsonString(const Result& r) {
    return toJson(r).dump(2);
}

inline std::string toJsonString(const Command& c) {
    return toJson(c).dump(2);
}

} // namespace caudio::cli
