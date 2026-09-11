#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <iostream>
#include <utility>

import caudio.cli;
import caudio.app;

int main(int argc, char** argv) {
    // Load default config (XDG or temp). If path empty, use default.
    auto cfgExp = caudio::cli::loadConfig(std::filesystem::path{});
    caudio::cli::Config cfg;
    if (cfgExp) {
        cfg = std::move(*cfgExp);
    } else {
        std::cerr << std::format("warning: loadConfig failed {} {}\n",
                                 std::to_string(std::to_underlying(cfgExp.error().code)),
                                 cfgExp.error().message);
        cfg.dbPath = std::filesystem::path("library.db");
        cfg.configPath = std::filesystem::path{};
        cfg.device = "auto";
        cfg.logLevel = 2;
    }

    caudio::app::App app{std::move(cfg)};
    return app.run(argc, argv);
}




