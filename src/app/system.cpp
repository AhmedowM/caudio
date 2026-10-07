/**
 * @file system.cpp
 * @brief History and audio-device command handlers.
 * @ingroup caudio_app
 * @details Moved verbatim out of the CLI shell (Phase 0).
 */

#include <caudio/app/core.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <expected>
#include <format>
#include <optional>
#include <string>

namespace caudio::app {

int App::historyList(int limit, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::HistoryList{limit}};
    return sendViaClient(cmd, asJson);
}

int App::historyClear(bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::HistoryClear{}};
    return confirm(sendRaw(cmd), asJson, "History cleared");
}

int App::deviceList(bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::DeviceList{}};
    return sendViaClient(cmd, asJson);
}

int App::deviceSet(const std::string& id, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::DeviceSet{id}};
    return confirm(sendRaw(cmd), asJson, std::format("Default device: {}", id));
}

int App::deviceTest(std::optional<std::string> id, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::DeviceTest{id}};
    std::string line = id.has_value() ? std::format("Device available: {}", *id)
                                      : "Default device available";
    return confirm(sendRaw(cmd), asJson, line);
}

} // namespace caudio::app
