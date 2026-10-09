/**
 * @file transport.cpp
 * @brief Transport policy: sends, autostart, confirmation rendering.
 * @ingroup caudio_app
 * @details Moved verbatim out of the CLI shell (Phase 0). Every command
 * goes through sendRaw (error normalization); play-like commands go
 * through sendPlay (daemon autostart + settle retry); confirm renders the
 * one-line success contract shared by most commands.
 */

#include <caudio/app/core.hpp>
#include <caudio/client/core.hpp>
#include <caudio/client/output_formatter.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/print.hpp>
#include <caudio/utils/result.hpp>
#include <chrono>
#include <expected>
#include <iostream>
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

int App::printErr(const caudio::utils::Error& e) {
    caudio::ipc::Result errRes{e};
    caudio::client::OutputFormatter fmt{false};
    fmt.print(errRes, std::cerr);
    if (e.code == caudio::utils::StatusCode::State && e.message == "daemon not running")
        caudio::println(std::cerr, "hint: run `caudio start` to start the daemon");
    return 1;
}

int App::printJson(const caudio::ipc::Result& r) {
    caudio::client::OutputFormatter fmt{true};
    fmt.print(r, std::cout);
    return 0;
}

int App::sendViaClient(const caudio::ipc::Command& cmd, bool asJson) {
    auto res = sendRaw(cmd);
    if (!res)
        return printErr(res.error());
    if (asJson)
        return printJson(*res);
    caudio::client::OutputFormatter fmt{false};
    fmt.print(*res, std::cout);
    return 0;
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

// Custom confirmation: JSON dumps the raw result, text prints `line`.
int App::confirm(std::expected<caudio::ipc::Result, caudio::utils::Error>&& res, bool asJson,
                 const std::string& line) {
    if (!res)
        return printErr(res.error());
    if (asJson)
        return printJson(*res);
    caudio::println(std::cout, "{}", line);
    return 0;
}

AppResult Outcome::warn(std::string message) {
    return Outcome{std::nullopt, std::move(message), true, false};
}

AppResult Outcome::fail(std::string message) {
    return Outcome{std::nullopt, std::move(message), true, true};
}

AppResult App::confirm(std::expected<caudio::ipc::Result, caudio::utils::Error>&& res,
                       std::optional<std::string> line) {
    auto owned = std::move(res);
    if (!owned)
        return std::unexpected{owned.error()};
    return Outcome{std::move(*owned), std::move(line), false};
}

} // namespace caudio::app
