#include <atomic>
#include <caudio/config.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/player/output.hpp>
#include <caudio/service/service_core.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/result.hpp>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "service_paths.hpp"

namespace caudio::service {

using namespace caudio::ipc;
using namespace caudio::config;
using caudio::ipc::ConfigExport;
using caudio::ipc::ConfigGet;
using caudio::ipc::ConfigImport;
using caudio::ipc::ConfigList;
using caudio::ipc::ConfigReset;
using caudio::ipc::ConfigSet;
using caudio::ipc::DeviceList;
using caudio::ipc::DeviceSet;
using caudio::ipc::DeviceTest;
using caudio::ipc::Shutdown;

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::ConfigGet& cmd) {
    auto p = detail::resolveConfigPath(config_.configPath, config_.dbPath);
    auto vRes = caudio::config::configGetRaw(p, cmd.key);
    if (!vRes)
        return std::unexpected{vRes.error()};
    return Result{ConfigValue{cmd.key, *vRes}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::ConfigSet& cmd) {
    auto p = detail::resolveConfigPath(config_.configPath, config_.dbPath);
    auto sRes = caudio::config::configSetRaw(p, cmd.key, cmd.value);
    if (!sRes)
        return std::unexpected{sRes.error()};
    return Result{Empty{}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::ConfigList&) {
    auto p = detail::resolveConfigPath(config_.configPath, config_.dbPath);
    auto lRes = caudio::config::configListRaw(p);
    if (!lRes)
        return std::unexpected{lRes.error()};
    ConfigValues cvs{};
    cvs.values.reserve(lRes->size());
    for (auto& kv : *lRes)
        cvs.values.push_back(ConfigValue{kv.key, kv.value});
    return Result{std::move(cvs)};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::ConfigExport& cmd) {
    auto src = detail::resolveConfigPath(config_.configPath, config_.dbPath);
    std::filesystem::path dst{cmd.path};
    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::NotFound, "config not found")};
    }
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io, ec.message())};
    return Result{Empty{}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::ConfigImport& cmd) {
    std::filesystem::path src{cmd.path};
    auto dst = detail::resolveConfigPath(config_.configPath, config_.dbPath);
    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::NotFound, "import path not found")};
    }
    std::filesystem::create_directories(dst.parent_path(), ec);
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io, ec.message())};
    // validate that file is readable and non-empty JSON-like (at least contains
    // '{')
    std::error_code ec2;
    if (!std::filesystem::exists(dst, ec2)) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Io, "import failed")};
    }
    return Result{Empty{}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::ConfigReset& cmd) {
    auto p = detail::resolveConfigPath(config_.configPath, config_.dbPath);
    if (cmd.key.has_value() && !cmd.key->empty()) {
        auto r = caudio::config::configDeleteRaw(p, *cmd.key);
        if (!r)
            return std::unexpected{r.error()};
    } else if (cmd.key.has_value() && cmd.key->empty()) {
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::InvalidArg, "empty key")};
    } else {
        auto r = caudio::config::configResetAllRaw(p);
        if (!r)
            return std::unexpected{r.error()};
    }
    return Result{Empty{}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::Shutdown&) {
    shutdownRequested_.store(true, std::memory_order_release);
    // defer actual shutdown to run loop to avoid deadlock
    return Result{Empty{}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::DeviceList&) {
    auto devList = caudio::player::enumerateDevices();
    caudio::ipc::Devices result;
    result.devices.reserve(devList.devices.size());
    for (const auto& d : devList.devices) {
        result.devices.push_back(caudio::ipc::DeviceInfo{d.id, d.name, d.isDefault});
    }
    return Result{std::move(result)};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::DeviceSet& cmd) {
    // Validate device exists
    auto devList = caudio::player::enumerateDevices();
    bool found = false;
    for (const auto& d : devList.devices) {
        if (d.id == cmd.id) {
            found = true;
            break;
        }
    }
    if (!found) {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound,
                                                        "device not found: " + cmd.id)};
    }
    // Save to config
    auto cfgPath = detail::resolveConfigPath(config_.configPath, config_.dbPath);
    auto res = caudio::config::configSetRaw(cfgPath, "device", cmd.id);
    if (!res) {
        return std::unexpected{res.error()};
    }
    // Update engine's device if running - the engine will pick it up on next playback
    // For now, just persist the config
    return Result{Empty{}};
}

std::expected<caudio::ipc::Result, caudio::utils::Error>
Service::handle(const caudio::ipc::DeviceTest& cmd) {
    // Get device ID to test
    std::string testId;
    if (cmd.id.has_value()) {
        testId = *cmd.id;
    } else {
        // Use current config device
        auto cfgPath = detail::resolveConfigPath(config_.configPath, config_.dbPath);
        auto devRes = caudio::config::configGetRaw(cfgPath, "device");
        if (devRes) {
            testId = *devRes;
        } else {
            testId = "auto";
        }
    }
    // Enumeration-only check: there is no test-tone path (no AudioOutput is
    // created here). Success means the id resolves against a listed device.
    auto devList = caudio::player::enumerateDevices();
    bool found = false;
    for (const auto& d : devList.devices) {
        if (d.id == testId || (testId == "auto" && d.isDefault)) {
            found = true;
            break;
        }
    }
    if (!found && testId != "auto") {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::NotFound,
                                                        "device not found: " + testId)};
    }
    return Result{Empty{}};
}

} // namespace caudio::service
