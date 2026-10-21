/**
 * @file system.cpp
 * @brief History and audio-device command handlers.
 * @ingroup caudio_app
 */

#include <caudio/app/app.hpp>
#include <caudio/app/format.hpp>
#include <caudio/client/client.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/utils/error.hpp>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <variant>

namespace caudio::app {

AppResult App::historyList(int limit) {
    caudio::ipc::Command cmd{caudio::ipc::HistoryList{limit}};
    return confirm(sendRaw(cmd));
}

AppResult App::historyClear() {
    caudio::ipc::Command cmd{caudio::ipc::HistoryClear{}};
    return confirm(sendRaw(cmd), "History cleared");
}

AppResult App::bookmarkAdd(std::optional<double> position, std::string note,
                           std::optional<int64_t> track) {
    double pos = 0.0;
    if (position.has_value()) {
        pos = *position;
    } else {
        caudio::client::Client probe{config_.dbPath, config_.socketPath};
        auto ps = probe.send(caudio::ipc::Command{caudio::ipc::StatusReq{}});
        if (!ps)
            return std::unexpected{ps.error()};
        auto* st = std::get_if<caudio::ipc::Status>(&*ps);
        if (!st) {
            if (auto* e = std::get_if<caudio::utils::Error>(&*ps))
                return std::unexpected{*e};
            return std::unexpected{
                caudio::utils::makeError(caudio::utils::StatusCode::State, "status unavailable")};
        }
        pos = st->pos;
    }
    if (pos < 0.0)
        pos = 0.0;
    int64_t posMs = (int64_t)std::llround(pos * 1000.0);
    caudio::ipc::Command cmd{caudio::ipc::BookmarkAdd{posMs, std::move(note), track}};
    auto res = sendRaw(cmd);
    if (!res)
        return std::unexpected{res.error()};
    std::string line = std::format("Bookmark added at {}", caudio::app::fmtClock(pos));
    return Outcome{
        .result = std::move(*res), .line = std::move(line), .toStderr = false, .failed = false};
}

AppResult App::bookmarkList(std::optional<int64_t> track) {
    caudio::ipc::Command cmd{caudio::ipc::BookmarkList{track}};
    return confirm(sendRaw(cmd));
}

AppResult App::bookmarkRemove(int64_t id) {
    caudio::ipc::Command cmd{caudio::ipc::BookmarkRemove{id}};
    return confirm(sendRaw(cmd), std::format("Bookmark {} removed", id));
}

AppResult App::bookmarkGoto(int64_t id) {
    caudio::ipc::Command cmd{caudio::ipc::BookmarkGoto{id}};
    auto res = sendRaw(cmd);
    if (!res)
        return std::unexpected{res.error()};
    if (auto* st = std::get_if<caudio::ipc::Status>(&*res)) {
        std::string who = caudio::app::trackWho(*st);
        std::string line = std::format("Resuming {} from {}", who, caudio::app::fmtClock(st->pos));
        return Outcome{
            .result = std::move(*res), .line = std::move(line), .toStderr = false, .failed = false};
    }
    return Outcome{
        .result = std::move(*res), .line = std::nullopt, .toStderr = false, .failed = false};
}

AppResult App::deviceList() {
    caudio::ipc::Command cmd{caudio::ipc::DeviceList{}};
    return confirm(sendRaw(cmd));
}

AppResult App::deviceSet(const std::string& id) {
    caudio::ipc::Command cmd{caudio::ipc::DeviceSet{id}};
    return confirm(sendRaw(cmd), std::format("Default device: {}", id));
}

AppResult App::deviceTest(std::optional<std::string> id) {
    caudio::ipc::Command cmd{caudio::ipc::DeviceTest{id}};
    std::string line =
        id.has_value() ? std::format("Device available: {}", *id) : "Default device available";
    return confirm(sendRaw(cmd), std::move(line));
}

} // namespace caudio::app
