/**
 * @file transport.cpp
 * @brief Transport policy: sends, autostart, confirmation data.
 * @ingroup caudio_app
 * @details Every command goes through sendRaw (error normalization);
 * play-like commands go through sendPlay (daemon autostart + settle
 * retry); confirm pairs results with their success lines. Frontends
 * render the returned outcomes.
 */

#include <caudio/app/core.hpp>
#include <caudio/client/core.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <chrono>
#include <expected>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace caudio::app {

std::expected<caudio::ipc::Result, caudio::utils::Error>
App::sendRaw(const caudio::ipc::Command& cmd, std::chrono::milliseconds timeout) {
    caudio::client::Client client{config_.dbPath, config_.socketPath};
    auto res = client.send(cmd, timeout);
    if (!res)
        return std::unexpected{res.error()};
    if (std::holds_alternative<caudio::utils::Error>(*res))
        return std::unexpected{std::get<caudio::utils::Error>(*res)};
    return std::move(*res);
}

// Play-like sends autostart the daemon when it is down (quick launch).
std::expected<caudio::ipc::Result, caudio::utils::Error>
App::sendPlay(const caudio::ipc::Command& cmd) {
    auto res = sendRaw(cmd);
    if (!res) {
        const auto& e = res.error();
        if (e.code == caudio::utils::StatusCode::State && e.message == "daemon not running") {
            if (!startDaemon(false)) {
                // The autostart may have raced a dying daemon (lock held
                // at spawn). One more attempt after a short settle delay;
                // surface the freshest error, not the original "down".
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                auto retry = sendRaw(cmd);
                if (retry)
                    return retry;
                return std::unexpected{retry.error()};
            }
            return sendRaw(cmd);
        }
    }
    return res;
}

AppResult Outcome::warn(std::string message) {
    return Outcome{std::nullopt, std::move(message), true, false};
}

AppResult Outcome::fail(std::string message) {
    return Outcome{std::nullopt, std::move(message), true, true};
}

BatchResult BatchReport::fail(std::string message) {
    return BatchReport{std::nullopt, "", std::move(message), 1};
}

AppResult App::confirm(std::expected<caudio::ipc::Result, caudio::utils::Error>&& res,
                       std::optional<std::string> line) {
    auto owned = std::move(res);
    if (!owned)
        return std::unexpected{owned.error()};
    return Outcome{std::move(*owned), std::move(line), false};
}

} // namespace caudio::app
