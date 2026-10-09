/**
 * @file library.cpp
 * @brief Library command handlers (scan/search/stats/list/add/remove).
 * @ingroup caudio_app
 * @details Phase 1: returns outcome data; the frontend renders.
 */

#include <app/detail.hpp>
#include <caudio/app/core.hpp>
#include <caudio/app/format.hpp>
#include <caudio/client/output_formatter.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <cctype>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace caudio::app {

AppResult App::libraryScan(std::optional<std::string> path, bool fullHash) {
    caudio::ipc::Command cmd{caudio::ipc::LibraryScan{path, fullHash}};
    return confirm(sendRaw(cmd));
}

AppResult App::librarySearch(const std::string& query, int limit) {
    caudio::ipc::Command cmd{caudio::ipc::LibrarySearch{query, limit}};
    return confirm(sendRaw(cmd));
}

BatchResult App::libraryStats(int mostPlayed, const std::vector<std::string>& queues,
                              const std::vector<std::int64_t>& playlists) {
    if (mostPlayed < -1)
        return BatchReport::fail("library stats: --most-played needs N >= 0");
    BatchReport rep;
    std::optional<caudio::ipc::Result> table;
    bool detailed = mostPlayed >= 0;
    if (detailed) {
        caudio::ipc::Command cmd{caudio::ipc::LibraryStatsDetailed{mostPlayed}};
        auto res = sendRaw(cmd);
        if (!res)
            return std::unexpected{res.error()};
        table = std::move(*res);
    } else {
        caudio::ipc::Command cmd{caudio::ipc::LibraryStats{}};
        auto res = sendRaw(cmd);
        if (!res)
            return std::unexpected{res.error()};
        table = std::move(*res);
    }
    rep.json = std::move(table);
    {
        caudio::client::OutputFormatter fmt{false};
        std::ostringstream os;
        fmt.print(*rep.json, os);
        detail::appendBlock(rep.out, os.str());
    }
    if (!queues.empty() || !playlists.empty())
        rep.out += '\n';
    if (!queues.empty()) {
        caudio::ipc::Command cmd{caudio::ipc::QueueQueues{}};
        auto res = sendRaw(cmd);
        if (!res)
            return std::unexpected{res.error()};
        auto* qs = std::get_if<caudio::ipc::Queues>(&*res);
        if (!qs) {
            detail::renderInto(rep.out, *res);
            return rep;
        }
        bool all = false;
        for (auto& q : queues) {
            if (q == "all") {
                all = true;
                break;
            }
        }
        if (all) {
            detail::renderInto(rep.out, *res);
        } else {
            for (auto& q : queues) {
                long long id = 0;
                try {
                    id = std::stoll(q);
                } catch (...) {
                    return BatchReport::fail(
                        std::format("library stats: bad queue selector '{}'", q));
                }
                bool found = false;
                for (auto& e : qs->entries) {
                    if (e.id == id) {
                        if (!rep.out.empty())
                            rep.out += '\n';
                        rep.out += std::format("Queue {} '{}': {} tracks", e.id, e.name, e.tracks);
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return BatchReport::fail(std::format("library stats: no such queue: {}", id));
            }
        }
    }
    for (auto pid : playlists) {
        caudio::ipc::Command cmd{caudio::ipc::PlaylistTracks{pid}};
        auto res = sendRaw(cmd);
        if (!res)
            return std::unexpected{res.error()};
        auto* pd = std::get_if<caudio::ipc::PlaylistData>(&*res);
        if (!pd) {
            detail::renderInto(rep.out, *res);
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
        if (name.empty())
            return BatchReport::fail(std::format("library stats: no such playlist: {}", pid));
        if (!rep.out.empty())
            rep.out += '\n';
        if (pd->tracks.size() == 1)
            rep.out += std::format("Playlist '{}': 1 track", name);
        else
            rep.out += std::format("Playlist '{}': {} tracks", name, pd->tracks.size());
    }
    return rep;
}

AppResult App::libraryList(const std::string& query, int limit, int offset,
                           const std::string& artist, const std::string& album,
                           const std::string& genre) {
    caudio::ipc::Command cmd{caudio::ipc::LibraryList{
        query.empty() ? std::optional<std::string>{} : std::optional<std::string>{query}, limit,
        offset, artist.empty() ? std::optional<std::string>{} : std::optional<std::string>{artist},
        album.empty() ? std::optional<std::string>{} : std::optional<std::string>{album},
        genre.empty() ? std::optional<std::string>{} : std::optional<std::string>{genre}}};
    return confirm(sendRaw(cmd));
}

AppResult App::libraryAdd(const std::string& path, bool recursive) {
    caudio::ipc::Command cmd{caudio::ipc::LibraryAdd{path, recursive}};
    return confirm(sendRaw(cmd), std::format("Added {} to library", path));
}

AppResult App::libraryRemove(const std::string& query) {
    // Resolve a label first so the confirmation names it.
    std::string label;
    bool numeric = !query.empty();
    for (char c : query) {
        if (!std::isdigit((unsigned char)c)) {
            numeric = false;
            break;
        }
    }
    if (numeric) {
        long long id = 0;
        try {
            id = std::stoll(query);
        } catch (...) {
            id = 0;
        }
        if (id != 0) {
            caudio::ipc::Command cmd{caudio::ipc::TagGet{id}};
            if (auto res = sendRaw(cmd)) {
                if (auto* st = std::get_if<caudio::ipc::SingleTrack>(&*res))
                    label = caudio::app::addedLabel(st->track);
            }
        }
    }
    if (label.empty()) {
        std::string fn = std::filesystem::path(query).filename().generic_string();
        if (!fn.empty())
            label = fn;
    }
    caudio::ipc::Command cmd{caudio::ipc::LibraryRemove{query}};
    auto res = sendRaw(cmd);
    if (!res)
        return std::unexpected{res.error()};
    std::string line = !label.empty() ? std::format("Removed from library {}", label)
                                      : std::format("Removed {} from library", query);
    return Outcome{std::move(*res), std::move(line), false, false};
}

} // namespace caudio::app
