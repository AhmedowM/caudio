#include <caudio/config.hpp>
#include <caudio/utils/result.hpp>
#include <expected>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <utility>

#include "core.hpp"

int main(int argc, char** argv) {
    // Load default config (XDG or temp). If path empty, use default.
    auto cfgExp = caudio::config::loadConfig(std::filesystem::path{});
    caudio::config::Config cfg;
    if (cfgExp) {
        cfg = std::move(*cfgExp);
    } else {
        std::cerr << std::format("warning: loadConfig failed ({}): {}\n",
                                 caudio::utils::toString(cfgExp.error().code),
                                 cfgExp.error().message);
        cfg.dbPath = std::filesystem::path("library.db");
        cfg.configPath = std::filesystem::path{};
        cfg.device = "auto";
        cfg.logLevel = 2;
    }

    caudio::app::App app{std::move(cfg)};
    return app.run(argc, argv);
}
