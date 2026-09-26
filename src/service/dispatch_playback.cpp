#include <algorithm>
#include <atomic>
#include <caudio/config.hpp>
#include <caudio/db.hpp>
#include <caudio/engine.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/protocol.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/player.hpp>
#include <caudio/service/ipc_channel.hpp>
#include <caudio/service/ipc_server.hpp>
#include <caudio/service/service_impl.hpp>
#include <caudio/service/shm_status.hpp>
#include <caudio/utils.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>


namespace caudio::service {

using namespace caudio::ipc;
using caudio::ipc::Next;
using caudio::ipc::Pause;
using caudio::ipc::Play;
using caudio::ipc::Prev;
using caudio::ipc::Restart;
using caudio::ipc::Resume;
using caudio::ipc::Seek;
using caudio::ipc::StatusReq;
using caudio::ipc::Stop;
using caudio::ipc::VolumeSet;

// Play command: start/resume playback of active queue
std::expected<caudio::ipc::Result, caudio::utils::Error> Service::handle(const caudio::ipc::Play&) {
    int64_t aq = engine_->activeQueueId();
    auto r = engine_->play(aq);
    if (!r)
        return std::unexpected{r.error()};
    updateShmStatus();
    return statusResult();
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::Pause&) {
    auto r = engine_->pause();
    if (!r)
        return std::unexpected{r.error()};
    updateShmStatus();
    return statusResult();
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::Resume&) {
    auto r = engine_->resume();
    if (!r)
        return std::unexpected{r.error()};
    updateShmStatus();
    return statusResult();
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::Restart&) {
    // restart: seek to 0, ensure playing
    auto r = engine_->seek(0.0);
    if (!r) {
        // if no track, try play
        int64_t aq = engine_->activeQueueId();
        auto pr = engine_->play(aq);
        if (!pr)
            return std::unexpected{pr.error()};
        updateShmStatus();
        return statusResult();
    }
    // ensure playing
    if (engine_->state() == caudio::engine::PlaybackState::Paused) {
        (void)engine_->resume();
    }
    updateShmStatus();
    return statusResult();
}

std::expected<caudio::ipc::Result, caudio::utils::Error> Service::handle(const caudio::ipc::Stop&) {
    auto r = engine_->stop();
    if (!r)
        return std::unexpected{r.error()};
    updateShmStatus();
    return statusResult();
}

std::expected<caudio::ipc::Result, caudio::utils::Error> Service::handle(const caudio::ipc::Next&) {
    auto r = engine_->next();
    if (!r)
        return std::unexpected{r.error()};
    updateShmStatus();
    return statusResult();
}

std::expected<caudio::ipc::Result, caudio::utils::Error> Service::handle(const caudio::ipc::Prev&) {
    auto r = engine_->prev();
    if (!r)
        return std::unexpected{r.error()};
    updateShmStatus();
    return statusResult();
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::Seek& s) {
    if (!std::isfinite(s.seconds) || s.seconds < 0) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg,
                                                        "seek: invalid seconds")};
    }
    auto r = engine_->seek(s.seconds);
    if (!r)
        return std::unexpected{r.error()};
    updateShmStatus();
    return statusResult();
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::StatusReq&) {
    return statusResult();
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::VolumeSet& v) {
    float cur = engine_->volume();
    float target = cur;
    bool hasTarget = false;
    if (v.level.has_value()) {
        float lvl = *v.level;
        if (!std::isfinite(lvl)) {
            return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg,
                                                            "volume: invalid level")};
        }
        // clamp 0-100 -> 0.0-1.0
        if (lvl < 0.0f)
            lvl = 0.0f;
        if (lvl > 100.0f)
            lvl = 100.0f;
        target = lvl / 100.0f;
        hasTarget = true;
    }
    if (v.deltaPct.has_value()) {
        int d = *v.deltaPct;
        float curPct = cur * 100.0f;
        float np = curPct + static_cast<float>(d);
        if (np < 0.0f)
            np = 0.0f;
        if (np > 100.0f)
            np = 100.0f;
        target = np / 100.0f;
        hasTarget = true;
    }
    if (v.mute.has_value()) {
        if (*v.mute) {
            target = 0.0f;
            hasTarget = true;
        } else {
            if (cur == 0.0f && !hasTarget) {
                target = 0.5f;
                hasTarget = true;
            }
        }
    }
    if (hasTarget) {
        auto r = engine_->setVolume(target);
        if (!r)
            return std::unexpected{r.error()};
    }
    updateShmStatus();
    caudio::ipc::VolumeInfo vi{engine_->volume(), engine_->volume() == 0.0f};
    return Result{vi};
}

} // namespace caudio::service
