// TODO(Audit Directive 2 / B9, Appendix C §B9 & §2.2): remove <CLI/CLI.hpp> from public header — pulls 800+ KB macro-heavy CLI11 into every consumer (GUI/tests) and pollutes min/max macros. Forward-declare namespace CLI { class App; } and use std::unique_ptr<CLI::App> pImpl; keep CLI11 include only in cli/src/app/core.cpp. Subagent already tried but reverted; document for Phase 3 move to cli/src/app/core.hpp (private). See AUDIT_REPORT.md §B9, §3.2.
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

#include "caudio/db/database.hpp"
#include "caudio/json/json.hpp"
#include "caudio/utils/utils.hpp"
#include "cli/app/parse.hpp"
#include "cli/config.hpp"
#include "cli/shared/command.hpp"
#include "cli/shared/result.hpp"

namespace caudio::app {

namespace detail {

std::expected<double, caudio::utils::Error> parseTime(std::string_view s);
std::expected<double, caudio::utils::Error> parseSeek(std::string_view s);
std::expected<caudio::cli::VolumeSet, caudio::utils::Error> parseVolume(std::string_view s);
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
