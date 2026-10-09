/**
 * @file config.cpp
 * @brief Configuration command handlers.
 * @ingroup caudio_app
 * @details Phase 1: returns outcome data; the frontend renders. Reads
 * answer from the local file without a daemon; writes go through it.
 */

#include <caudio/app/core.hpp>
#include <caudio/config.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <expected>
#include <format>
#include <optional>
#include <string>

namespace caudio::app {

AppResult App::configGet(const std::string& key) {
    // Answered from the local file: no daemon needed.
    auto v = caudio::config::configGetRaw(config_.configPath, key);
    if (!v)
        return std::unexpected{v.error()};
    return confirm(caudio::ipc::Result{caudio::ipc::ConfigValue{key, *v}});
}

AppResult App::configSet(const std::string& key, const std::string& value) {
    caudio::ipc::Command cmd{caudio::ipc::ConfigSet{key, value}};
    auto res = sendRaw(cmd);
    if (!res)
        return std::unexpected{res.error()};
    return Outcome{};
}

AppResult App::configList() {
    caudio::ipc::Command cmd{caudio::ipc::ConfigList{}};
    return confirm(sendRaw(cmd));
}

AppResult App::configExport(const std::string& path) {
    caudio::ipc::Command cmd{caudio::ipc::ConfigExport{path}};
    return confirm(sendRaw(cmd), std::format("Exported config to {}", path));
}

AppResult App::configImport(const std::string& path) {
    caudio::ipc::Command cmd{caudio::ipc::ConfigImport{path}};
    return confirm(sendRaw(cmd), std::format("Imported config from {}", path));
}

AppResult App::configReset(std::optional<std::string> key) {
    caudio::ipc::Command cmd{caudio::ipc::ConfigReset{key}};
    std::string line =
        key.has_value() ? std::format("Reset config key '{}'", *key) : "Reset all config";
    return confirm(sendRaw(cmd), std::move(line));
}

} // namespace caudio::app
