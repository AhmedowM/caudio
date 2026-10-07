/**
 * @file queue.cpp
 * @brief Queue command handlers.
 * @ingroup caudio_app
 * @details Moved verbatim out of the CLI shell (Phase 0): listing with
 * playback-order highlight, switching, creation/deletion, the three add
 * modes (playlist / id-or-search / paths), removal by id/pos/path, move,
 * clear, shuffle and repeat. Flag-combination validation travels with the
 * handlers; glob expansion uses the shared paths utilities.
 */

#include <algorithm>
#include <app/detail.hpp>
#include <caudio/app/core.hpp>
#include <caudio/app/format.hpp>
#include <caudio/app/paths.hpp>
#include <caudio/client/core.hpp>
#include <caudio/client/output_formatter.hpp>
#include <caudio/db/types.hpp>
#include <caudio/engine/types.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/print.hpp>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace caudio::app {

int App::queues(bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::QueueQueues{}};
    return sendViaClient(cmd, asJson);
}

int App::queueTracks(const std::string& order, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::QueueList{order}};
    auto res = sendRaw(cmd);
    if (!res)
        return printErr(res.error());
    if (asJson)
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

int App::queueSwitch(std::int64_t qid, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::QueueSwitch{qid}};
    return confirm(sendRaw(cmd), asJson, std::format("Switched to queue {}", qid));
}

int App::queueCreate(const std::string& name, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::QueueCreate{name}};
    auto res = sendRaw(cmd);
    if (!res)
        return printErr(res.error());
    if (asJson)
        return printJson(*res);
    if (auto* qc = std::get_if<caudio::ipc::QueueCreated>(&*res)) {
        caudio::println("Created queue {} '{}'", qc->id, qc->name);
        return 0;
    }
    caudio::client::OutputFormatter fmt{false};
    fmt.print(*res, std::cout);
    return 0;
}

int App::queueDelete(std::int64_t qid, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::QueueDelete{qid}};
    return confirm(sendRaw(cmd), asJson, std::format("Deleted queue {}", qid));
}

int App::queueAdd(const std::vector<std::string>& paths, const std::string& id, bool search,
                  std::int64_t playlist, bool replace, bool recursive, bool asJson) {
    bool hasId = !id.empty();
    bool hasPlaylist = playlist != 0;
    if (replace && !hasPlaylist) {
        caudio::println(std::cerr, "queue add: --replace needs --playlist");
        return 1;
    }
    if (hasPlaylist && (hasId || search || !paths.empty())) {
        caudio::println(std::cerr, "queue add: --playlist takes no PATH, --id, or --search");
        return 1;
    }
    if (hasId && (!paths.empty() || search)) {
        caudio::println(std::cerr, "queue add: --id takes no PATH or --search");
        return 1;
    }
    if (search && paths.size() != 1) {
        caudio::println(std::cerr, "queue add: --search takes exactly one query");
        return 1;
    }
    if (hasId && !caudio::app::isNumeric(id)) {
        caudio::println(std::cerr, "queue add: --id needs a numeric library id");
        return 1;
    }
    if (!hasId && !search && !hasPlaylist && paths.empty()) {
        caudio::println(std::cerr, "queue add: need a PATH, --id ID, or --search QUERY");
        return 1;
    }
    if (hasPlaylist && replace) {
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
        caudio::ipc::Command tcmd{caudio::ipc::PlaylistTracks{playlist}};
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
            caudio::println(std::cerr, "queue add: playlist {} has no tracks", playlist);
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
            if (asJson) {
                if (auto* qt = std::get_if<caudio::ipc::QueueTracks>(&*res)) {
                    for (auto& at : qt->tracks)
                        collected.tracks.push_back(at);
                }
            } else {
                added += detail::printAdded(*res, seen);
            }
        }
        if (asJson)
            return printJson(caudio::ipc::Result{std::move(collected)});
        detail::countLine(added);
        return (added == 0) ? 1 : 0;
    }
    if (hasId || search) {
        caudio::ipc::Command cmd{caudio::ipc::QueueAdd{hasId ? id : paths[0], search}};
        auto res = sendRaw(cmd);
        if (!res)
            return printErr(res.error());
        if (asJson)
            return printJson(*res);
        int added = detail::printAdded(*res, seen);
        detail::countLine(added);
        return 0;
    }
    // PATH mode: expand globs/folders client-side.
    std::vector<std::string> files;
    std::vector<std::string> unmatched;
    for (auto& tok : paths)
        caudio::app::expandAddToken(tok, recursive, files, unmatched);
    bool hardFail = false;
    for (auto& u : unmatched) {
        if (caudio::app::isNumeric(u)) {
            caudio::println(std::cerr, "queue add: '{}' is not a file (use --id for library ids)",
                            u);
            hardFail = true;
        } else if (caudio::app::hasGlobChars(u)) {
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
        if (asJson) {
            if (auto* qt = std::get_if<caudio::ipc::QueueTracks>(&*res)) {
                for (auto& at : qt->tracks)
                    collected.tracks.push_back(at);
            }
        } else {
            added += detail::printAdded(*res, seen);
        }
    }
    if (asJson)
        return printJson(caudio::ipc::Result{std::move(collected)});
    detail::countLine(added);
    if (added == 0 && hardFail)
        return 1;
    return 0;
}

int App::queueRemove(const std::string& id, const std::string& pos,
                     const std::vector<std::string>& paths, bool recursive, bool asJson) {
    bool hasId = !id.empty();
    bool hasPos = !pos.empty();
    bool hasPaths = !paths.empty();
    int modes = (hasId ? 1 : 0) + (hasPos ? 1 : 0) + (hasPaths ? 1 : 0);
    if (modes == 0) {
        caudio::println(std::cerr, "queue remove: need PATH, --id ID, or --pos POS");
        return 1;
    }
    if (modes > 1) {
        caudio::println(std::cerr, "queue remove: PATH, --id, and --pos are exclusive");
        return 1;
    }
    if ((hasId && !caudio::app::isNumeric(id)) || (hasPos && !caudio::app::isNumeric(pos))) {
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
        if (!parseNum(pos, p))
            return 1;
        if (p < 0 || (std::size_t)p >= all.size()) {
            caudio::println(std::cerr, "queue remove: position out of range: {}", p);
            return 1;
        }
        targets.push_back(all[(std::size_t)p]);
    } else if (hasId) {
        long long idv = 0;
        if (!parseNum(id, idv))
            return 1;
        for (auto& t : all) {
            if (t.track.id == idv)
                targets.push_back(t);
        }
        if (targets.empty()) {
            caudio::println(std::cerr, "queue remove: track {} is not in the queue", idv);
            return 1;
        }
    } else {
        std::vector<std::string> files;
        std::vector<std::string> unmatched;
        for (auto& tok : paths)
            caudio::app::expandAddToken(tok, recursive, files, unmatched);
        for (auto& u : unmatched) {
            if (caudio::app::isNumeric(u)) {
                caudio::println(std::cerr,
                                "queue remove: '{}' is not a file (use --id/--pos for ids)", u);
                hardFail = true;
            } else if (caudio::app::hasGlobChars(u)) {
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
            std::string key = caudio::app::pathKey(f);
            bool found = false;
            for (std::size_t i = 0; i < all.size(); ++i) {
                if (!taken[i] && caudio::app::pathKey(all[i].track.path) == key) {
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
    std::sort(targets.begin(), targets.end(),
              [](const RemTarget& a, const RemTarget& b) { return a.pos > b.pos; });
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
        if (!asJson)
            caudio::println("Removed from queue {}", caudio::app::addedLabel(t.track));
        removedTracks.push_back(t.track);
        ++removed;
    }
    if (asJson)
        return printJson(caudio::ipc::Result{caudio::ipc::QueueTracks{std::move(removedTracks)}});
    if (removed == 0)
        return (targets.empty() && !hardFail) ? 0 : 1;
    return 0;
}

int App::queueMove(std::size_t from, std::size_t to, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::QueueMove{from, to}};
    auto res = sendRaw(cmd);
    if (!res)
        return printErr(res.error());
    if (asJson)
        return printJson(*res);
    std::size_t n = 0;
    if (auto* qt = std::get_if<caudio::ipc::QueueTracks>(&*res))
        n = qt->tracks.size();
    caudio::println("Moved to position {}. Queue: {} tracks", to, n);
    return 0;
}

int App::queueClear(bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::QueueClear{}};
    return confirm(sendRaw(cmd), asJson, "Queue cleared");
}

int App::queueShuffle(const std::string& mode, bool asJson) {
    std::optional<bool> on;
    if (mode == "on")
        on = true;
    else if (mode == "off")
        on = false;
    else if (!mode.empty()) {
        caudio::println(std::cerr, "shuffle: invalid mode '{}' (expected on|off)", mode);
        return 1;
    }
    caudio::ipc::Command cmd{caudio::ipc::QueueShuffle{on}};
    auto res = sendRaw(cmd);
    if (!res)
        return printErr(res.error());
    if (asJson)
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

int App::queueRepeat(const std::string& mode, bool asJson) {
    using RM = caudio::engine::RepeatMode;
    std::optional<RM> m;
    if (mode == "off")
        m = RM::Off;
    else if (mode == "one")
        m = RM::One;
    else if (mode == "all")
        m = RM::All;
    else if (!mode.empty()) {
        caudio::println(std::cerr, "repeat: invalid mode '{}' (expected off|one|all)", mode);
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
    if (asJson)
        return printJson(*res);
    RM finalMode = *m;
    if (auto* st = std::get_if<caudio::ipc::Status>(&*res))
        finalMode = st->repeat;
    caudio::println("Repeat: {}", finalMode == RM::All   ? "all"
                                  : finalMode == RM::One ? "one"
                                                         : "off");
    return 0;
}

} // namespace caudio::app
