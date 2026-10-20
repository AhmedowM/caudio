/**
 * @file preview.cpp
 * @brief Ephemeral local file preview dispatch (no daemon).
 * @ingroup caudio_app
 * @details Phase 1: returns outcome data; the frontend renders. The
 * player-backed implementation lives in the CLI shell (it owns the player
 * dependency) and is injected via App::setPreviewHost; this file only
 * dispatches to it.
 */

#include <caudio/app/app.hpp>
#include <string>
#include <utility>

namespace caudio::app {

void App::setPreviewHost(std::function<AppResult(const std::string& file)> host) {
    previewHost_ = std::move(host);
}

AppResult App::previewFile(const std::string& file) {
    if (previewHost_) {
        return previewHost_(file);
    }
    return Outcome::fail("preview host not configured");
}

} // namespace caudio::app
