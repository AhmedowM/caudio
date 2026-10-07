/**
 * @file playback.cpp
 * @brief Playback command handlers (transport, direct play, seek).
 * @ingroup caudio_app
 * @details Moved verbatim out of the CLI shell (Phase 0): shared play
 * flow, direct file play, transport commands and seeking. Argument *syntax*
 * (time/volume grammar, globs) stays in the shell; these take parsed values.
 */

#include <caudio/app/app.hpp>
#include <caudio/app/format.hpp>
#include <caudio/client/client_core.hpp>
#include <caudio/client/output_formatter.hpp>
#include <caudio/engine/engine_types.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/print.hpp>
#include <chrono>
#include <expected>
#include <string>
#include <variant>
#include <vector>

namespace caudio::app {

std::string App::transportWho(const std::expected<caudio::ipc::Result, caudio::utils::Error>& res) {
    if (auto* st = std::get_if<caudio::ipc::Status>(&*res))
        return caudio::app::trackWho(*st);
    return std::string{"unknown track"};
}

// Shared play flow (used by `play`, and by `resume` when stopped).
int App::doPlay(bool asJson) {
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
            caudio::println("Resuming {} from {}", who, caudio::app::fmtClock(priorPos));
        else
            caudio::println("Playing {}", who);
    } else {
        caudio::client::OutputFormatter fmt{false};
        fmt.print(*res, std::cout);
    }
    return 0;
}

int App::playFiles(const std::vector<std::string>& files, bool save) {
    caudio::ipc::Command cmd{caudio::ipc::PlayFiles{files, save}};
    auto res = sendPlay(cmd, false);
    if (!res)
        return printErr(res.error());
    if (auto* st = std::get_if<caudio::ipc::Status>(&*res)) {
        std::string who = caudio::app::trackWho(*st);
        if (save)
            caudio::println("Playing {}", who);
        else
            caudio::println("Playing {} (temporary queue)", who);
        return 0;
    }
    caudio::client::OutputFormatter fmt{false};
    fmt.print(*res, std::cout);
    return 0;
}

int App::pause(bool asJson) {
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
    if (asJson)
        return printJson(*res);
    if (std::get_if<caudio::ipc::Status>(&*res)) {
        std::string who = transportWho(res);
        double at = 0;
        if (auto* st = std::get_if<caudio::ipc::Status>(&*res))
            at = st->pos;
        caudio::println("Paused {} at {}", who, caudio::app::fmtClock(at));
    } else {
        caudio::client::OutputFormatter fmt{false};
        fmt.print(*res, std::cout);
    }
    return 0;
}

int App::resume(bool asJson) {
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
            return doPlay(asJson);
        }
        return printErr(e);
    }
    if (asJson)
        return printJson(*res);
    if (auto* st = std::get_if<caudio::ipc::Status>(&*res)) {
        caudio::println("Resuming {} from {}", caudio::app::trackWho(*st),
                        caudio::app::fmtClock(st->pos));
    } else {
        caudio::client::OutputFormatter fmt{false};
        fmt.print(*res, std::cout);
    }
    return 0;
}

int App::restart(bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::Restart{}};
    auto res = sendRaw(cmd);
    if (!res)
        return printErr(res.error());
    if (asJson)
        return printJson(*res);
    caudio::println("Restarting {}", transportWho(res));
    return 0;
}

int App::stop(bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::Stop{}};
    return confirmTransport(sendRaw(cmd), asJson, "Stopped");
}

int App::next(bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::Next{}};
    auto res = sendRaw(cmd);
    if (!res)
        return printErr(res.error());
    if (asJson)
        return printJson(*res);
    caudio::println("Playing {}", transportWho(res));
    return 0;
}

int App::prev(bool asJson) {
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
    if (asJson)
        return printJson(*res);
    caudio::println("Playing {}", transportWho(res));
    return 0;
}

int App::seek(double target, bool isRelative, bool asJson) {
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
    if (asJson)
        return printJson(*res);
    return 0;
}

} // namespace caudio::app
