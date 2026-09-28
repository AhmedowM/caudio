#pragma once

#include <caudio/config.hpp>
#include <caudio/db.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils.hpp>
#include <chrono>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "parse.hpp"

namespace CLI {
class App;
}

namespace caudio::app {

namespace detail {

std::expected<double, caudio::utils::Error> parseTime(std::string_view s);
std::expected<double, caudio::utils::Error> parseSeek(std::string_view s);
std::expected<caudio::ipc::VolumeSet, caudio::utils::Error> parseVolume(std::string_view s);
std::chrono::duration<double> parseDuration(std::string_view s);

void writePlaylistText(std::ostream& os, const std::vector<caudio::db::Track>& tracks,
                       std::string_view format);
void writePlaylistJson(std::ostream& os, const std::vector<caudio::db::Track>& tracks);

} // namespace detail

using detail::parseSeek;
using detail::parseTime;
using detail::parseVolume;

class App {
  public:
    explicit App(caudio::config::Config cfg);
    ~App();
    int run(int argc, char** argv);

  private:
    int handleStart(bool foreground);
    int handleShutdown();
    int handlePreview(const std::string& file);
    std::expected<void, std::uint32_t> spawnDaemon(const caudio::config::Config& cfg);
    std::filesystem::path pidPathForConfig() const;

    caudio::config::Config config_;
    std::unique_ptr<CLI::App> cli_;
    std::string argv0_;
};

} // namespace caudio::app
