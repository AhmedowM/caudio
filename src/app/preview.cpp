/**
 * @file preview.cpp
 * @brief Ephemeral local file preview (no daemon).
 * @ingroup caudio_app
 * @details Phase 1: returns outcome data; the frontend renders. Opens a
 * file in a throwaway player and blocks until it finishes. Frontends reuse
 * this for quick audition without touching the queue.
 */

#include <caudio/app/core.hpp>
#include <caudio/player/core.hpp>
#include <caudio/utils/error.hpp>
#include <chrono>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace caudio::app {

AppResult App::previewFile(const std::string& file) {
    std::filesystem::path p{file};
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        return Outcome::fail(std::format("preview: file not found {}", file));
    }
    auto playerRes = caudio::player::Player::create();
    if (!playerRes) {
        return Outcome::fail(std::format("preview: player create failed ({}): {}",
                                         caudio::utils::toString(playerRes.error().code),
                                         playerRes.error().message));
    }
    auto& player = *playerRes.value();
    auto openRes = player.open(file);
    if (!openRes) {
        return Outcome::fail(std::format("preview: open failed ({}): {}",
                                         caudio::utils::toString(openRes.error().code),
                                         openRes.error().message));
    }
    auto playRes = player.play();
    if (!playRes) {
        return Outcome::fail(std::format("preview: play failed ({}): {}",
                                         caudio::utils::toString(playRes.error().code),
                                         playRes.error().message));
    }
    std::string done = std::format("preview playing {}\npreview done", file);
    while (player.state() == caudio::player::State::Playing) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return Outcome{std::nullopt, std::move(done), false, false};
}

} // namespace caudio::app
