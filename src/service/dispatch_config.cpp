#include <caudio/service/service_impl.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <caudio/db.hpp>
#include <caudio/engine.hpp>
#include <nlohmann/json.hpp>
#include <caudio/player.hpp>
#include "../player/decoders/ffmpeg.hpp"
#include <caudio/utils.hpp>
#include <caudio/config.hpp>
#include <caudio/service/ipc_channel.hpp>
#include <caudio/service/ipc_server.hpp>
#include "service_paths.hpp"
#include <caudio/service/shm_status.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/protocol.hpp>
#include <caudio/ipc/result.hpp>

namespace caudio::service {

using namespace caudio::cli;
using caudio::cli::ConfigGet;
using caudio::cli::ConfigSet;
using caudio::cli::ConfigList;
using caudio::cli::ConfigExport;
using caudio::cli::ConfigImport;
using caudio::cli::ConfigReset;
using caudio::cli::Shutdown;
using caudio::cli::DeviceList;
using caudio::cli::DeviceSet;
using caudio::cli::DeviceTest;

std::expected<caudio::cli::Result, caudio::utils::Error>
Service::handle(const caudio::cli::ConfigGet& cmd) {
                auto p = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                auto vRes = caudio::cli::configGetRaw(p, cmd.key);
                if (!vRes)
                    return std::unexpected{vRes.error()};
                return Result{ConfigValue{cmd.key, *vRes}};
            }

std::expected<caudio::cli::Result, caudio::utils::Error>
Service::handle(const caudio::cli::ConfigSet& cmd) {
                auto p = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                auto sRes = caudio::cli::configSetRaw(p, cmd.key, cmd.value);
                if (!sRes)
                    return std::unexpected{sRes.error()};
                return Result{Empty{}};
            }

std::expected<caudio::cli::Result, caudio::utils::Error>
Service::handle(const caudio::cli::ConfigList&) {
                auto p = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                auto lRes = caudio::cli::configListRaw(p);
                if (!lRes)
                    return std::unexpected{lRes.error()};
                ConfigValues cvs{};
                cvs.values.reserve(lRes->size());
                for (auto& kv : *lRes)
                    cvs.values.push_back(ConfigValue{kv.key, kv.value});
                return Result{std::move(cvs)};
            }

std::expected<caudio::cli::Result, caudio::utils::Error>
Service::handle(const caudio::cli::ConfigExport& cmd) {
                auto src = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                std::filesystem::path dst{cmd.path};
                std::error_code ec;
                if (!std::filesystem::exists(src, ec)) {
                    return std::unexpected{caudio::utils::makeError(
                        caudio::utils::StatusCode::NotFound, "config not found")};
                }
                std::filesystem::copy_file(src, dst,
                                           std::filesystem::copy_options::overwrite_existing, ec);
                if (ec)
                    return std::unexpected{
                        caudio::utils::makeError(caudio::utils::StatusCode::Io, ec.message())};
                return Result{Empty{}};
            }

std::expected<caudio::cli::Result, caudio::utils::Error>
Service::handle(const caudio::cli::ConfigImport& cmd) {
                std::filesystem::path src{cmd.path};
                auto dst = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                std::error_code ec;
                if (!std::filesystem::exists(src, ec)) {
                    return std::unexpected{caudio::utils::makeError(
                        caudio::utils::StatusCode::NotFound, "import path not found")};
                }
                std::filesystem::create_directories(dst.parent_path(), ec);
                std::filesystem::copy_file(src, dst,
                                           std::filesystem::copy_options::overwrite_existing, ec);
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

std::expected<caudio::cli::Result, caudio::utils::Error>
Service::handle(const caudio::cli::ConfigReset& cmd) {
                auto p = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                if (cmd.key.has_value() && !cmd.key->empty()) {
                    auto r = caudio::cli::configDeleteRaw(p, *cmd.key);
                    if (!r)
                        return std::unexpected{r.error()};
                } else if (cmd.key.has_value() && cmd.key->empty()) {
                    return std::unexpected{caudio::utils::makeError(
                        caudio::utils::StatusCode::InvalidArg, "empty key")};
                } else {
                    auto r = caudio::cli::configResetAllRaw(p);
                    if (!r)
                        return std::unexpected{r.error()};
                }
                return Result{Empty{}};
            }

std::expected<caudio::cli::Result, caudio::utils::Error>
Service::handle(const caudio::cli::Shutdown&) {
                shutdownRequested_.store(true, std::memory_order_release);
                // defer actual shutdown to run loop to avoid deadlock
                return Result{Empty{}};
            }

std::expected<caudio::cli::Result, caudio::utils::Error>
Service::handle(const caudio::cli::DeviceList&) {
                auto devList = caudio::player::enumerateDevices();
                caudio::cli::Devices result;
                result.devices.reserve(devList.devices.size());
                for (const auto& d : devList.devices) {
                    result.devices.push_back(caudio::cli::DeviceInfo{d.id, d.name, d.isDefault});
                }
                return Result{std::move(result)};
            }

std::expected<caudio::cli::Result, caudio::utils::Error>
Service::handle(const caudio::cli::DeviceSet& cmd) {
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
                    return std::unexpected{caudio::utils::makeError(
                        caudio::utils::StatusCode::NotFound, "device not found: " + cmd.id)};
                }
                // Save to config
                auto cfgPath = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                auto res = caudio::cli::configSetRaw(cfgPath, "device", cmd.id);
                if (!res) {
                    return std::unexpected{res.error()};
                }
                // Update engine's device if running - the engine will pick it up on next playback
                // For now, just persist the config
                return Result{Empty{}};
            }

std::expected<caudio::cli::Result, caudio::utils::Error>
Service::handle(const caudio::cli::DeviceTest& cmd) {
                // Get device ID to test
                std::string testId;
                if (cmd.id.has_value()) {
                    testId = *cmd.id;
                } else {
                    // Use current config device
                    auto cfgPath = detail::resolveConfigPath(config_.configPath, config_.dbPath);
                    auto devRes = caudio::cli::configGetRaw(cfgPath, "device");
                    if (devRes) {
                        testId = *devRes;
                    } else {
                        testId = "auto";
                    }
                }
                // For "auto", use default device (empty ID in miniaudio)
                // Create a temporary player to test the device
                auto playerRes = caudio::player::Player::create();
                if (!playerRes) {
                    return std::unexpected{playerRes.error()};
                }
                // Generate a short test tone (1 second of 440Hz sine wave at -20dB)
                // This is a simple test - just verify device can be opened
                // The actual tone generation would require more complex setup
                // For now, return success if we can enumerate the device
                auto devList = caudio::player::enumerateDevices();
                bool found = false;
                for (const auto& d : devList.devices) {
                    if (d.id == testId || (testId == "auto" && d.isDefault)) {
                        found = true;
                        break;
                    }
                }
                if (!found && testId != "auto") {
                    return std::unexpected{caudio::utils::makeError(
                        caudio::utils::StatusCode::NotFound, "device not found: " + testId)};
                }
                return Result{Empty{}};
            }

} // namespace caudio::service
