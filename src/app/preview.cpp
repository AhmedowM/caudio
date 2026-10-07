/**
 * @file preview.cpp
 * @brief Ephemeral local file preview (no daemon).
 * @ingroup caudio_app
 * @details Moved verbatim out of the CLI shell (Phase 0): opens a file in
 * a throwaway player and blocks until it finishes. Frontends reuse this
 * for quick audition without touching the queue.
 */

#include <caudio/app/app.hpp>
#include <caudio/player/player_core.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/print.hpp>
#include <caudio/utils/result.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>

namespace caudio::app {

int App::previewFile(const std::string& file) {
    std::filesystem::path p{file};
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        caudio::println(std::cerr, "preview: file not found {}", file);
        return 1;
    }
    auto playerRes = caudio::player::Player::create();
    if (!playerRes) {
        caudio::println(std::cerr, "preview: player create failed ({}): {}",
                        caudio::utils::toString(playerRes.error().code), playerRes.error().message);
        return 1;
    }
    auto& player = *playerRes.value();
    auto openRes = player.open(file);
    if (!openRes) {
        caudio::println(std::cerr, "preview: open failed ({}): {}",
                        caudio::utils::toString(openRes.error().code), openRes.error().message);
        return 1;
    }
    auto playRes = player.play();
    if (!playRes) {
        caudio::println(std::cerr, "preview: play failed ({}): {}",
                        caudio::utils::toString(playRes.error().code), playRes.error().message);
        return 1;
    }
    caudio::println("preview playing {}", file);
    while (player.state() == caudio::player::State::Playing) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    caudio::println("preview done");
    return 0;
}

} // namespace caudio::app
