#include <caudio/config.hpp>
#include <caudio/utils/result.hpp>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <utility>

#include "core.hpp"

#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#include <process.h>
// Debug-only abort tripwire: the Debug CRT shows a modal dialog on
// abort()/terminate and blocks the process, which then looks like a deaf
// daemon or a hung CLI (and cascades into start/shutdown races). Route the
// report to a log file with pid + exception type instead, so a future abort
// fails fast with evidence instead of hanging behind a dialog.
namespace {
FILE* g_abortLog() {
    static FILE* f = []() -> FILE* {
        FILE* fh = nullptr;
        auto p = std::filesystem::temp_directory_path() / "caudio-abort.log";
        _wfopen_s(&fh, p.c_str(), L"a");
        return fh;
    }();
    return f;
}
void g_logAbort(const char* what) {
    if (FILE* f = g_abortLog()) {
        std::fprintf(f, "pid=%lu %s\n", static_cast<unsigned long>(::_getpid()), what);
        std::fflush(f);
    }
}
void g_terminateDiag() {
    try {
        throw;
    } catch (const std::exception& e) {
        std::string m = std::string("terminate: std::exception: ") + e.what();
        g_logAbort(m.c_str());
    } catch (...) {
        g_logAbort("terminate: unknown non-std exception");
    }
    std::abort();
}
} // namespace
#endif

int main(int argc, char** argv) {
#if defined(_WIN32) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, g_abortLog());
    _set_abort_behavior(0, _WRITE_ABORT_MSG);
    std::set_terminate(g_terminateDiag);
#endif
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
