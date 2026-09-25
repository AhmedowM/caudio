// TODO(Audit Directive 2 / B9, Appendix C Â§B9 & Â§2.2): remove <caudio/ipc.hpp> from public header â€” pulls 800+ KB macro-heavy CLI11 into every consumer (GUI/tests) and pollutes min/max macros. Forward-declare namespace CLI { class App; } and use std::unique_ptr<CLI::App> pImpl; keep CLI11 include only in cli/src/app/core.cpp. Subagent already tried but reverted; document for Phase 3 move to cli/src/app/core.hpp (private). See AUDIT_REPORT.md Â§B9, Â§3.2.
#pragma once

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

#include <caudio/db.hpp>
#include <nlohmann/json.hpp>
#include <caudio/utils.hpp>
#include "parse.hpp"
#include <caudio/config.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>

namespace CLI {
class App;
}

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
    ~App();
    int run(int argc, char** argv);

  private:
    int handleStart(bool foreground);
    int handleShutdown();
    int handlePreview(const std::string& file);
    std::expected<void, std::uint32_t> spawnDaemon(const caudio::cli::Config& cfg);
    std::filesystem::path pidPathForConfig() const;

    caudio::cli::Config config_;
    std::unique_ptr<CLI::App> cli_;
    std::string argv0_;
};

} // namespace caudio::app
