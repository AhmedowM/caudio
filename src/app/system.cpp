/**
 * @file system.cpp
 * @brief History and audio-device command handlers.
 * @ingroup caudio_app
 */

#include <caudio/app/core.hpp>
#include <caudio/ipc/command.hpp>
#include <format>
#include <optional>
#include <string>

namespace caudio::app {

AppResult App::historyList(int limit) {
    caudio::ipc::Command cmd{caudio::ipc::HistoryList{limit}};
    return confirm(sendRaw(cmd));
}

AppResult App::historyClear() {
    caudio::ipc::Command cmd{caudio::ipc::HistoryClear{}};
    return confirm(sendRaw(cmd), "History cleared");
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
