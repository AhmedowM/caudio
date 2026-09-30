#include <algorithm>
#include <array>
#include <caudio/db/db_types.hpp>
#include <caudio/db/scan.hpp>
#include <caudio/db/search.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/player/decoder.hpp>
#include <caudio/service/service_core.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/json.hpp>
#include <caudio/utils/print.hpp>
#include <caudio/utils/result.hpp>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <db/fingerprint.hpp>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "service_audio.hpp"
#include "service_status.hpp"

namespace caudio::service {

using namespace caudio::ipc;
using caudio::ipc::HistoryClear;
using caudio::ipc::HistoryList;
using caudio::ipc::Info;
using caudio::ipc::LibraryAdd;
using caudio::ipc::LibraryList;
using caudio::ipc::LibraryRemove;
using caudio::ipc::LibraryScan;
using caudio::ipc::LibrarySearch;
using caudio::ipc::LibraryStats;
using caudio::ipc::LibraryStatsDetailed;
using caudio::ipc::PlaylistDelete;
using caudio::ipc::PlaylistExport;
using caudio::ipc::PlaylistImport;
using caudio::ipc::PlaylistList;
using caudio::ipc::PlaylistLoad;
using caudio::ipc::PlaylistRename;
using caudio::ipc::PlaylistSave;
using caudio::ipc::PlaylistTracks;
using caudio::ipc::Preview;
using caudio::ipc::TagEdit;
using caudio::ipc::TagGet;

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::LibraryScan& cmd) {
    std::filesystem::path root;
    if (cmd.path)
        root = std::filesystem::path(*cmd.path);
    else {
        auto pp = config_.dbPath.parent_path();
        if (pp.empty())
            pp = std::filesystem::current_path();
        root = pp / "music";
    }
    auto mode = (cmd.mode == "full" ? caudio::db::ScanMode::Full : caudio::db::ScanMode::Sampled);
    // Prefer scanLibrary if a library matches root -- gives dedup + batched
    // transaction
    if (auto libs = db_->libraryList(); libs) {
        for (auto& l : *libs) {
            if (std::filesystem::path(l.path) == root) {
                auto sr = caudio::db::scanLibrary(*db_, l.id);
                if (!sr)
                    return std::unexpected{sr.error()};
                caudio::ipc::LibraryStatsData d2{};
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
    caudio::ipc::LibraryStatsData d{};
    d.tracks = n;
    d.queues = 0;
    d.playlists = 0;
    if (auto st = db_->getStats()) {
        d.queues = static_cast<std::size_t>(st->num_queue_items);
        d.playlists = static_cast<std::size_t>(st->num_playlists);
    }
    return Result{std::move(d)};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::LibrarySearch& cmd) {
    auto tracks = caudio::db::search(*db_, cmd.query, cmd.limit);
    if (!tracks)
        return std::unexpected{tracks.error()};
    // fallback to like handled inside search; if empty still return
    std::span<const caudio::db::Track> span{*tracks};
    std::vector<caudio::db::Track> out(span.begin(), span.end());
    return Result{Tracks{std::move(out)}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::LibraryStats&) {
    auto st = db_->getStats();
    if (!st)
        return std::unexpected{st.error()};
    caudio::ipc::LibraryStatsData d{};
    d.tracks = static_cast<std::size_t>(st->num_tracks);
    d.queues = static_cast<std::size_t>(st->num_queue_items);
    d.playlists = static_cast<std::size_t>(st->num_playlists);
    return Result{d};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::LibraryStatsDetailed&) {
    auto st = db_->libraryStatsDetailed();
    if (!st)
        return std::unexpected{st.error()};
    caudio::ipc::LibraryStatsDetailedData d{};
    d.tracks = static_cast<std::size_t>(st->tracks);
    d.queues = static_cast<std::size_t>(st->queues);
    d.playlists = static_cast<std::size_t>(st->playlists);
    d.total_duration_ms = st->total_duration_ms;
    d.total_play_time_ms = st->total_play_time_ms;
    d.most_played = std::move(st->most_played);
    return Result{std::move(d)};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::LibraryList& cmd) {
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
}

std::expected<caudio::ipc::Result, caudio::utils::Error> Service::handle(const caudio::ipc::Info&) {
    // Get current track from engine status
    int64_t track_id = engine_->currentTrackId();
    if (track_id == 0) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound,
                                                        "no track currently playing")};
    }
    auto tr = db_->getTrack(track_id);
    if (!tr)
        return std::unexpected{tr.error()};
    caudio::ipc::TrackInfo ti{};
    ti.track = std::move(*tr);
    ti.play_count = ti.track.play_count;
    ti.last_played = ti.track.last_played;
    return Result{std::move(ti)};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::HistoryList& cmd) {
    int limit = cmd.limit.value_or(50);
    auto hist = engine_->listHistory(limit);
    if (!hist)
        return std::unexpected{hist.error()};
    std::vector<caudio::ipc::HistoryEntry> cliEntries;
    cliEntries.reserve(hist->size());
    for (const auto& e : *hist) {
        caudio::ipc::HistoryEntry cliEntry;
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
    return Result{caudio::ipc::History{std::move(cliEntries)}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::HistoryClear&) {
    auto r = engine_->clearHistory();
    if (!r)
        return std::unexpected{r.error()};
    return Result{Empty{}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::LibraryAdd& cmd) {
    if (cmd.path.empty())
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg,
                                                        "library add: missing path")};
    std::filesystem::path p(cmd.path);
    std::error_code ec;
    bool exists = std::filesystem::exists(p, ec);
    if (ec || !exists)
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound,
                                                        "path not found: " + cmd.path)};
    auto addSingleFile =
        [&](const std::filesystem::path& fp) -> std::expected<void, caudio::utils::Error> {
        if (!caudio::db::detail::hasAudioExt(fp))
            return std::unexpected{caudio::utils::makeError(
                caudio::utils::StatusCode::Unsupported, "unsupported file type: " + fp.string())};
        auto fpRes = caudio::db::internal::computeFingerprint(fp);
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
            if (meta->duration > 0)
                t.duration = meta->duration;
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
                if (it->is_regular_file(e3) && !e3 && caudio::db::detail::hasAudioExt(it->path())) {
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
                if (it->is_regular_file(e3) && !e3 && caudio::db::detail::hasAudioExt(it->path())) {
                    auto r = addSingleFile(it->path());
                    if (r)
                        ++added;
                }
            }
        }
        if (added == 0) {
            // check if any audio files existed but failed?
            // Return Empty still if dir was empty -- not an error.
        }
        return Result{Empty{}};
    } else {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg,
                                                        "not a file or directory: " + cmd.path)};
    }
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::LibraryRemove& cmd) {
    if (cmd.query.empty())
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg,
                                                        "library remove: missing id")};
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
    return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound,
                                                    "track not found: " + cmd.query)};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::TagEdit& cmd) {
    if (cmd.id == 0)
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg,
                                                        "tag edit: invalid id")};
    static const std::array<std::string_view, 8> allowed{
        "title", "artist", "album", "album_artist", "genre", "year", "track_number", "disc_number"};
    bool ok = false;
    for (auto a : allowed)
        if (a == cmd.field) {
            ok = true;
            break;
        }
    if (!ok)
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg,
                                                        "invalid field: " + cmd.field)};
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
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg,
                                                            "invalid year: " + cmd.value)};
        }
    } else if (cmd.field == "track_number") {
        try {
            size_t pos = 0;
            int v = std::stoi(cmd.value, &pos);
            if (pos != cmd.value.size())
                throw std::invalid_argument("extra");
            t.track_num = v;
        } catch (...) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg,
                                                            "invalid track_number: " + cmd.value)};
        }
    } else if (cmd.field == "disc_number") {
        try {
            size_t pos = 0;
            int v = std::stoi(cmd.value, &pos);
            if (pos != cmd.value.size())
                throw std::invalid_argument("extra");
            t.disc_num = v;
        } catch (...) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg,
                                                            "invalid disc_number: " + cmd.value)};
        }
    }
    auto upd = db_->updateTrack(t);
    if (!upd)
        return std::unexpected{upd.error()};
    return Result{Empty{}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::TagGet& cmd) {
    if (cmd.id == 0)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "tag get: invalid id")};
    auto tr = db_->getTrack(cmd.id);
    if (!tr)
        return std::unexpected{tr.error()};
    return Result{SingleTrack{std::move(*tr)}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::PlaylistList&) {
    auto pls = db_->listPlaylists();
    if (!pls)
        return std::unexpected{pls.error()};
    std::span<const caudio::db::Playlist> span{*pls};
    std::vector<caudio::db::Playlist> out(span.begin(), span.end());
    return Result{Playlists{std::move(out)}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::PlaylistTracks& cmd) {
    auto tracks = db_->playlistGetTracks(cmd.pid);
    if (!tracks)
        return std::unexpected{tracks.error()};
    std::span<const caudio::db::Track> span{*tracks};
    std::vector<caudio::db::Track> out(span.begin(), span.end());
    return Result{QueueTracks{std::move(out)}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::PlaylistLoad& cmd) {
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
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::PlaylistSave& cmd) {
    if (cmd.name.empty())
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "empty playlist name")};
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
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::PlaylistDelete& cmd) {
    auto r = db_->deletePlaylist(cmd.pid);
    if (!r)
        return std::unexpected{r.error()};
    return Result{Empty{}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::PlaylistRename& cmd) {
    if (cmd.newName.empty())
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "empty playlist name")};
    auto r = db_->renamePlaylist(cmd.pid, cmd.newName);
    if (!r)
        return std::unexpected{r.error()};
    return Result{Empty{}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::PlaylistExport& cmd) {
    auto tracks = db_->playlistGetTracks(cmd.pid);
    if (!tracks)
        return std::unexpected{tracks.error()};
    std::vector<caudio::db::Track> out;
    out.reserve(tracks->size());
    for (auto& t : std::span<const caudio::db::Track>(*tracks))
        out.push_back(std::move(t));
    return Result{PlaylistData{std::move(out), cmd.format}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::PlaylistImport& cmd) {
    std::filesystem::path path{cmd.path};
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound,
                                                        "file not found: " + cmd.path)};
    }
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    std::vector<std::string> lines;
    if (ext == ".json") {
        // Parse JSON format
        std::ifstream ifs(path);
        if (!ifs)
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io, "failed to open file")};
        std::string content((std::istreambuf_iterator<char>(ifs)),
                            std::istreambuf_iterator<char>());
        try {
            const caudio::utils::Json j = caudio::utils::Json::parse(content);
            if (j.contains("tracks") && j["tracks"].isArray()) {
                const caudio::utils::Json tracks = j["tracks"];
                for (std::size_t ti = 0, tn = tracks.size(); ti < tn; ++ti) {
                    auto trackExp = tracks.at(ti);
                    if (!trackExp)
                        continue;
                    const caudio::utils::Json track = std::move(*trackExp);
                    if (track.contains("path") && track["path"].isString()) {
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
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::Io, "failed to open file")};
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
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound,
                                                        "no matching tracks found in library")};
    }
    std::string name = cmd.name.value_or(path.stem().string());
    auto pidRes = db_->createPlaylistFromTracks(name, trackIds);
    if (!pidRes)
        return std::unexpected{pidRes.error()};
    caudio::ipc::PlaylistData pd{};
    auto tracksRes = db_->playlistGetTracks(*pidRes);
    if (tracksRes) {
        for (auto& t : std::span<const caudio::db::Track>(*tracksRes))
            pd.tracks.push_back(std::move(t));
    }
    pd.format = ext;
    if (skipped > 0) {
        caudio::println(std::cerr, "playlist import: skipped {} unmatched tracks", skipped);
    }
    caudio::println(std::cerr, "playlist import: matched {} tracks, created playlist '{}' (id={})",
                    matched, name, *pidRes);
    return Result{std::move(pd)};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::Preview&) {
    return Result{Empty{}};
}

} // namespace caudio::service
