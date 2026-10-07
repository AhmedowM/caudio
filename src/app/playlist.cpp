/**
 * @file playlist.cpp
 * @brief Playlist command handlers.
 * @ingroup caudio_app
 * @details Moved verbatim out of the CLI shell (Phase 0): listing, track
 * contents, creation, adding (ids and paths with library resolution),
 * loading, saving, deletion, renaming, file export and import.
 */

#include <app/detail.hpp>
#include <caudio/app/core.hpp>
#include <caudio/app/format.hpp>
#include <caudio/app/paths.hpp>
#include <caudio/client/core.hpp>
#include <caudio/client/output_formatter.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/print.hpp>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

namespace caudio::app {

int App::playlistList(bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::PlaylistList{}};
    return sendViaClient(cmd, asJson);
}

int App::playlistTracks(std::int64_t pid, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::PlaylistTracks{pid}};
    return sendViaClient(cmd, asJson);
}

int App::playlistCreate(const std::string& name, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::PlaylistCreate{name}};
    auto res = sendRaw(cmd);
    if (!res)
        return printErr(res.error());
    if (asJson)
        return printJson(*res);
    if (auto* pc = std::get_if<caudio::ipc::PlaylistCreated>(&*res)) {
        caudio::println("Created playlist {} '{}'", pc->id, pc->name);
        return 0;
    }
    caudio::client::OutputFormatter fmt{false};
    fmt.print(*res, std::cout);
    return 0;
}

int App::playlistAdd(std::int64_t pid, const std::vector<std::string>& ids,
                     const std::vector<std::string>& paths, bool recursive, bool asJson) {
    if (ids.empty() && paths.empty()) {
        caudio::println(std::cerr, "playlist add: need --id ID or PATH");
        return 1;
    }
    std::vector<int64_t> resolved;
    for (auto& s : ids) {
        if (!caudio::app::isNumeric(s)) {
            caudio::println(std::cerr, "playlist add: --id needs numeric ids");
            return 1;
        }
        try {
            resolved.push_back(std::stoll(s));
        } catch (...) {
            caudio::println(std::cerr, "playlist add: id out of range: {}", s);
            return 1;
        }
    }
    // PATHs: expand, ensure library rows, resolve exact ids.
    std::vector<std::string> files;
    std::vector<std::string> unmatched;
    for (auto& tok : paths)
        caudio::app::expandAddToken(tok, recursive, files, unmatched);
    bool hardFail = false;
    for (auto& u : unmatched) {
        if (caudio::app::isNumeric(u)) {
            caudio::println(std::cerr, "playlist add: '{}' is not a file (use --id for ids)", u);
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
            if (caudio::app::pathKey(t.path) == caudio::app::pathKey(f)) {
                resolved.push_back(t.id);
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
    for (auto tid : resolved) {
        caudio::ipc::Command cmd{caudio::ipc::PlaylistAdd{pid, tid}};
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
        if (asJson) {
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
                    label = caudio::app::addedLabel(st->track);
            }
            if (label.empty())
                label = "track " + std::to_string(tid);
            caudio::println("Added {} to playlist {}", label, pid);
        }
        ++added;
    }
    if (asJson)
        return printJson(caudio::ipc::Result{std::move(collected)});
    if (added == 1)
        caudio::println("1 track added");
    else
        caudio::println("{} tracks added", added);
    if (added == 0 && hardFail)
        return 1;
    return 0;
}

int App::playlistLoad(std::int64_t pid, bool play, bool replace, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::PlaylistLoad{pid, play, replace}};
    auto res = sendRaw(cmd);
    if (!res)
        return printErr(res.error());
    if (asJson)
        return printJson(*res);
    if (auto* pl = std::get_if<caudio::ipc::PlaylistLoaded>(&*res)) {
        if (replace)
            caudio::println("Replaced queue {} with playlist {}", pl->queue_id, pid);
        else
            caudio::println("Loaded playlist {} into queue {}", pid, pl->queue_id);
        if (play)
            caudio::println("Playing {}", caudio::app::trackWho(pl->status));
        return 0;
    }
    caudio::client::OutputFormatter fmt{false};
    fmt.print(*res, std::cout);
    return 0;
}

int App::playlistSave(const std::string& name, std::optional<std::int64_t> qid, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::PlaylistSave{name, qid}};
    std::string line = std::format("Saved playlist '{}'", name);
    return confirm(sendRaw(cmd), asJson, line);
}

int App::playlistDelete(std::int64_t pid, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::PlaylistDelete{pid}};
    return confirm(sendRaw(cmd), asJson, std::format("Deleted playlist {}", pid));
}

int App::playlistRename(std::int64_t pid, const std::string& name, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::PlaylistRename{pid, name}};
    return confirm(sendRaw(cmd), asJson, std::format("Renamed playlist {} to '{}'", pid, name));
}

int App::playlistExport(std::int64_t pid, const std::string& path, const std::string& format) {
    caudio::ipc::Command cmd{caudio::ipc::PlaylistExport{pid, path, format}};
    caudio::client::Client client{config_.dbPath, config_.socketPath};
    auto timeout = std::chrono::milliseconds{5000};
    auto cliRes = client.send(cmd, timeout);
    if (!cliRes) {
        caudio::println(std::cerr, "export: {}", cliRes.error().message);
        return 1;
    }
    if (std::holds_alternative<caudio::utils::Error>(*cliRes)) {
        caudio::println(std::cerr, "export: {}", std::get<caudio::utils::Error>(*cliRes).message);
        return 1;
    }
    if (std::holds_alternative<caudio::ipc::PlaylistData>(*cliRes)) {
        auto& pd = std::get<caudio::ipc::PlaylistData>(*cliRes);
        std::ofstream ofs(path);
        if (!ofs) {
            caudio::println(std::cerr, "export: failed to open output file '{}'", path);
            return 1;
        }
        if (pd.format == "m3u" || pd.format == "pls") {
            caudio::app::writePlaylistText(ofs, pd.tracks, pd.format);
        } else if (pd.format == "json") {
            caudio::app::writePlaylistJson(ofs, pd.tracks);
        }
        ofs.close();
        caudio::println("exported {} tracks to {}", pd.tracks.size(), path);
        return 0;
    }
    caudio::println(std::cerr, "export: unexpected response from daemon");
    return 1;
}

int App::playlistImport(const std::string& path, std::optional<std::string> name, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::PlaylistImport{path, name}};
    return sendViaClient(cmd, asJson);
}

} // namespace caudio::app
