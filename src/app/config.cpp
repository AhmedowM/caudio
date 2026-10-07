/**
 * @file config.cpp
 * @brief Configuration command handlers.
 * @ingroup caudio_app
 * @details Moved verbatim out of the CLI shell (Phase 0). Reads answer
 * from the local file without a daemon; writes go through the daemon.
 */

#include <caudio/app/core.hpp>
#include <caudio/client/output_formatter.hpp>
#include <caudio/config.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/print.hpp>
#include <expected>
#include <format>
#include <iostream>
#include <optional>
#include <string>

namespace caudio::app {

int App::configGet(const std::string& key, bool asJson) {
    // Answered from the local file: no daemon needed.
    auto v = caudio::config::configGetRaw(config_.configPath, key);
    if (!v)
        return printErr(v.error());
    caudio::ipc::Result r{caudio::ipc::ConfigValue{key, *v}};
    if (asJson)
        return printJson(r);
    caudio::client::OutputFormatter fmt{false};
    fmt.print(r, std::cout);
    return 0;
}

int App::configSet(const std::string& key, const std::string& value) {
    caudio::ipc::Command cmd{caudio::ipc::ConfigSet{key, value}};
    auto res = sendRaw(cmd);
    if (!res)
        return printErr(res.error());
    return 0;
}

int App::configList(bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::ConfigList{}};
    return sendViaClient(cmd, asJson);
}

int App::configExport(const std::string& path, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::ConfigExport{path}};
    return confirm(sendRaw(cmd), asJson, std::format("Exported config to {}", path));
}

int App::configImport(const std::string& path, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::ConfigImport{path}};
    return confirm(sendRaw(cmd), asJson, std::format("Imported config from {}", path));
}

int App::configReset(std::optional<std::string> key, bool asJson) {
    caudio::ipc::Command cmd{caudio::ipc::ConfigReset{key}};
    std::string line =
        key.has_value() ? std::format("Reset config key '{}'", *key) : "Reset all config";
    return confirm(sendRaw(cmd), asJson, line);
}

} // namespace caudio::app
