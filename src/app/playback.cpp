/**
 * @file playback.cpp
 * @brief Playback command handlers (transport, direct play, seek).
 * @ingroup caudio_app
 * @details Phase 1: returns outcome data; the frontend renders. Shared play
 * flow, direct file play, transport commands and seeking. Argument *syntax*
 * (time/volume grammar, globs) stays in the shell; these take parsed values.
 */

#include <caudio/app/core.hpp>
#include <caudio/app/format.hpp>
#include <caudio/client/core.hpp>
#include <caudio/engine/types.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <chrono>
#include <expected>
#include <format>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace caudio::app {

std::string App::transportWho(const std::expected<caudio::ipc::Result, caudio::utils::Error>& res) {
    if (auto* st = std::get_if<caudio::ipc::Status>(&*res))
        return caudio::app::trackWho(*st);
    return std::string{"unknown track"};
}

// Shared play flow (used by `play`, and by `resume` when stopped).
AppResult App::doPlay() {
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
    auto res = sendPlay(cmd);
    if (!res)
        return std::unexpected{res.error()};
    if (std::get_if<caudio::ipc::Status>(&*res)) {
        std::string who = transportWho(res);
        std::string line =
            wasPaused ? std::format("Resuming {} from {}", who, caudio::app::fmtClock(priorPos))
                      : std::format("Playing {}", who);
        return Outcome{
            .result = std::move(*res), .line = std::move(line), .toStderr = false, .failed = false};
    }
    return Outcome{
        .result = std::move(*res), .line = std::nullopt, .toStderr = false, .failed = false};
}

AppResult App::playFiles(const std::vector<std::string>& files, bool save) {
    caudio::ipc::Command cmd{caudio::ipc::PlayFiles{files, save}};
    auto res = sendPlay(cmd);
    if (!res)
        return std::unexpected{res.error()};
    if (auto* st = std::get_if<caudio::ipc::Status>(&*res)) {
        std::string who = caudio::app::trackWho(*st);
        std::string line = save ? std::format("Playing {}", who)
                                : std::format("Playing {} (temporary queue)", who);
        return Outcome{
            .result = std::move(*res), .line = std::move(line), .toStderr = false, .failed = false};
    }
    return Outcome{
        .result = std::move(*res), .line = std::nullopt, .toStderr = false, .failed = false};
}

AppResult App::pause() {
    caudio::ipc::Command cmd{caudio::ipc::Pause{}};
    auto res = sendRaw(cmd);
    if (!res) {
        const auto& e = res.error();
        if (e.code == caudio::utils::StatusCode::State && e.message == "not playing")
            return Outcome::warn("pause: nothing playing");
        return std::unexpected{e};
    }
    if (auto* st = std::get_if<caudio::ipc::Status>(&*res)) {
        std::string who = transportWho(res);
        return Outcome{.result = std::move(*res),
                       .line = std::format("Paused {} at {}", who, caudio::app::fmtClock(st->pos)),
                       .toStderr = false,
                       .failed = false};
    }
    return Outcome{
        .result = std::move(*res), .line = std::nullopt, .toStderr = false, .failed = false};
}

AppResult App::resume() {
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
            if (probed && playing)
                return Outcome::warn("resume: already playing");
            if (!probed)
                return std::unexpected{e};
            return doPlay();
        }
        return std::unexpected{e};
    }
    if (auto* st = std::get_if<caudio::ipc::Status>(&*res)) {
        // Hoisted: the line must be built before Outcome moves *res (the
        // braced list otherwise reads moved-from strings on some toolchains).
        std::string who = caudio::app::trackWho(*st);
        std::string at = caudio::app::fmtClock(st->pos);
        return Outcome{.result = std::move(*res),
                       .line = std::format("Resuming {} from {}", who, at),
                       .toStderr = false,
                       .failed = false};
    }
    return Outcome{
        .result = std::move(*res), .line = std::nullopt, .toStderr = false, .failed = false};
}

AppResult App::restart() {
    caudio::ipc::Command cmd{caudio::ipc::Restart{}};
    auto res = sendRaw(cmd);
    if (!res)
        return std::unexpected{res.error()};
    // Hoisted: read before Outcome moves *res (see resume()).
    std::string who = transportWho(res);
    return Outcome{.result = std::move(*res),
                   .line = std::format("Restarting {}", who),
                   .toStderr = false,
                   .failed = false};
}

AppResult App::stop() {
    caudio::ipc::Command cmd{caudio::ipc::Stop{}};
    return confirm(sendRaw(cmd), "Stopped");
}

AppResult App::next() {
    caudio::ipc::Command cmd{caudio::ipc::Next{}};
    auto res = sendRaw(cmd);
    if (!res && res.error().code == caudio::utils::StatusCode::Busy) {
        // Transient queue-lock contention (racing gapless advance or a
        // sibling command): retry once after a beat before surfacing it.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        res = sendRaw(cmd);
    }
    if (!res) {
        const auto& e = res.error();
        if (e.code == caudio::utils::StatusCode::Busy)
            return Outcome::fail("next: engine busy, retry");
        return std::unexpected{e};
    }
    // Hoisted: read before Outcome moves *res (see resume()).
    std::string who = transportWho(res);
    return Outcome{.result = std::move(*res),
                   .line = std::format("Playing {}", who),
                   .toStderr = false,
                   .failed = false};
}

AppResult App::prev() {
    caudio::ipc::Command cmd{caudio::ipc::Prev{}};
    auto res = sendRaw(cmd);
    if (!res && res.error().code == caudio::utils::StatusCode::Busy) {
        // Same transient contention as next(): retry once (see above).
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        res = sendRaw(cmd);
    }
    if (!res) {
        const auto& e = res.error();
        if (e.code == caudio::utils::StatusCode::Busy)
            return Outcome::fail("prev: engine busy, retry");
        if (e.code == caudio::utils::StatusCode::NotFound && e.message == "at start")
            return Outcome::warn("prev: at queue start");
        return std::unexpected{e};
    }
    // Hoisted: read before Outcome moves *res (see resume()).
    std::string who = transportWho(res);
    return Outcome{.result = std::move(*res),
                   .line = std::format("Playing {}", who),
                   .toStderr = false,
                   .failed = false};
}

AppResult App::seek(double target, bool isRelative) {
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
        return std::unexpected{res.error()};
    return Outcome{.result = std::move(*res),
                   .line = std::nullopt,
                   .toStderr = false,
                   .failed = false,
                   .quiet = true};
}

} // namespace caudio::app
