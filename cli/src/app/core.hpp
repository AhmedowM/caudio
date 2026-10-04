#pragma once

#include <caudio/config.hpp>
#include <caudio/db/db_types.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/utils/error.hpp>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace CLI {
class App;
}

namespace caudio::app {

namespace detail {

// CLI-internal helpers (adapt parse.hpp string-errors into utils::Error).

/// @brief Parses `s`/`mm:ss`/`hh:mm:ss` into seconds (see parse.hpp).
std::expected<double, caudio::utils::Error> parseTime(std::string_view s);
/// @brief Parses absolute or `+`/`-` relative seeks into seconds-or-delta.
std::expected<double, caudio::utils::Error> parseSeek(std::string_view s);
/// @brief Parses `0-100`/`+n`/`-n`/`mute`/`unmute`/empty(show) into VolumeSet.
std::expected<caudio::ipc::VolumeSet, caudio::utils::Error> parseVolume(std::string_view s);
/// @brief parseTime result as a duration.
std::chrono::duration<double> parseDuration(std::string_view s);

/// @brief Writes tracks as M3U/PLS/plain text to `os`.
void writePlaylistText(std::ostream& os, const std::vector<caudio::db::Track>& tracks,
                       std::string_view format);
/// @brief Writes tracks as `{"format":"caudio-playlist",...}` JSON to `os`.
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
    int handleStart(bool foreground, bool quiet = false);
    int handleShutdown();
    int handlePreview(const std::string& file);
    std::expected<void, std::uint32_t> spawnDaemon(const caudio::config::Config& cfg);
    std::filesystem::path pidPathForConfig() const;

    caudio::config::Config config_;
    std::unique_ptr<CLI::App> cli_;
    std::string argv0_;
};

} // namespace caudio::app
