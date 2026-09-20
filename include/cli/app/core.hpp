#pragma once

#include <CLI/CLI.hpp>
#include <chrono>
#include <expected>
#include <filesystem>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cli/app/parse.hpp"

#include "caudio/utils/utils.hpp"
#include "caudio/cli/config.hpp"
#include "caudio/cli/shared/command.hpp"
#include "caudio/cli/shared/result.hpp"
#include "caudio/db/database.hpp"
#include "caudio/json/json.hpp"

namespace caudio::app {

namespace detail {

std::expected<double, caudio::utils::Error> parseTime(std::string_view s);
std::expected<double, caudio::utils::Error> parseSeek(std::string_view s);
std::expected<caudio::cli::VolumeSet, caudio::utils::Error> parseVolume(std::string_view s);
std::chrono::duration<double> parseDuration(std::string_view s);

void writePlaylistText(std::ostream& os, const std::vector<caudio::db::Track>& tracks, std::string_view format);
void writePlaylistJson(std::ostream& os, const std::vector<caudio::db::Track>& tracks);

} // namespace detail

using detail::parseSeek;
using detail::parseTime;
using detail::parseVolume;

class App {
public:
    explicit App(caudio::cli::Config cfg);
    int run(int argc, char** argv);

private:
    int handleStart(bool foreground);
    int handleShutdown();
    int handlePreview(const std::string& file);
    std::expected<void, std::uint32_t> spawnDaemon(const caudio::cli::Config& cfg);
    std::filesystem::path pidPathForConfig() const;

    caudio::cli::Config config_;
    CLI::App cli_;
    std::string argv0_;
};

} // namespace caudio::app