/**
 * @file library.cpp
 * @brief Library command handlers (scan/search/stats/list/add/remove).
 * @ingroup caudio_app
 * @details Moved verbatim out of the CLI shell (Phase 0).
 */

#include <app/detail.hpp>
#include <caudio/app/app.hpp>
#include <caudio/app/format.hpp>
#include <caudio/client/output_formatter.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/print.hpp>
#include <cctype>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace caudio::app {

int App::libraryScan(std::optional<std::string> path, bool fullHash, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::LibraryScan{path, fullHash}};
    return sendViaClient(cmd, asJson);
}

int App::librarySearch(const std::string& query, int limit, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::LibrarySearch{query, limit}};
    auto res = sendRaw(cmd);
    if (!res)
        return printErr(res.error());
    if (asJson)
        return printJson(*res);
    caudio::client::OutputFormatter fmt{false, detail::useColor()};
    fmt.setHighlightNeedle(query);
    fmt.print(*res, std::cout);
    return 0;
}

int App::libraryStats(int mostPlayed, const std::vector<std::string>& queues,
                      const std::vector<std::int64_t>& playlists, bool asJson) {
    bool detailed = mostPlayed >= 0;
    if (asJson && (!queues.empty() || !playlists.empty())) {
        caudio::println(std::cerr, "library stats: --json takes no --queue or --playlist");
        return 1;
    }
    if (mostPlayed < -1) {
        caudio::println(std::cerr, "library stats: --most-played needs N >= 0");
        return 1;
    }
    if (detailed) {
        caudio::ipc::Command cmd{caudio::ipc::LibraryStatsDetailed{mostPlayed}};
        auto res = sendRaw(cmd);
        if (!res)
            return printErr(res.error());
        if (asJson)
            return printJson(*res);
        caudio::client::OutputFormatter fmt{false};
        fmt.print(*res, std::cout);
    } else {
        caudio::ipc::Command cmd{caudio::ipc::LibraryStats{}};
        auto res = sendRaw(cmd);
        if (!res)
            return printErr(res.error());
        if (asJson)
            return printJson(*res);
        caudio::client::OutputFormatter fmt{false};
        fmt.print(*res, std::cout);
    }
    if (!queues.empty() || !playlists.empty())
        caudio::println("");
    if (!queues.empty()) {
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
        for (auto& q : queues) {
            if (q == "all") {
                all = true;
                break;
            }
        }
        if (all) {
            caudio::client::OutputFormatter fmt{false};
            fmt.print(*res, std::cout);
        } else {
            for (auto& q : queues) {
                long long id = 0;
                try {
                    id = std::stoll(q);
                } catch (...) {
                    caudio::println(std::cerr, "library stats: bad queue selector '{}'", q);
                    return 1;
                }
                bool found = false;
                for (auto& e : qs->entries) {
                    if (e.id == id) {
                        caudio::println("Queue {} '{}': {} tracks", e.id, e.name, e.tracks);
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
    for (auto pid : playlists) {
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

int App::libraryList(const std::string& query, int limit, int offset, const std::string& artist,
                     const std::string& album, const std::string& genre, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::LibraryList{
        query.empty() ? std::optional<std::string>{} : std::optional<std::string>{query}, limit,
        offset,
        artist.empty() ? std::optional<std::string>{} : std::optional<std::string>{artist},
        album.empty() ? std::optional<std::string>{} : std::optional<std::string>{album},
        genre.empty() ? std::optional<std::string>{} : std::optional<std::string>{genre}}};
    return sendViaClient(cmd, asJson);
}

int App::libraryAdd(const std::string& path, bool recursive, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::LibraryAdd{path, recursive}};
    return confirm(sendRaw(cmd), asJson, std::format("Added {} to library", path));
}

int App::libraryRemove(const std::string& query, bool asJson) {
    // Resolve a label first so the confirmation names the track.
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
        return printErr(res.error());
    if (asJson)
        return printJson(*res);
    if (!label.empty())
        caudio::println("Removed from library {}", label);
    else
        caudio::println("Removed {} from library", query);
    return 0;
}

} // namespace caudio::app
