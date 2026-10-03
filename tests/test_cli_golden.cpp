#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <common.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <cstdlib>
#else
#include <sys/wait.h>
#endif

#ifndef TEST_CLI_BIN
#error "TEST_CLI_BIN must be defined to the caudio executable under test"
#endif

using namespace caudio::test_helpers;

namespace {

// ---------------------------------------------------------------------------
// Minimal cross-platform process runner: '<bin> args...' with stdout/stderr
// captured to temp files. Quotes every argument; works under cmd and sh.
// ---------------------------------------------------------------------------

struct RunResult {
    int exitCode{-1};
    std::string out{};
    std::string err{};
};

std::string shellQuote(const std::string& s) {
    std::string q = "\"";
    for (char c : s) {
        if (c == '"')
            q += "\\\"";
        else
            q += c;
    }
    q += "\"";
    return q;
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

RunResult runCli(const std::vector<std::string>& args) {
    namespace fs = std::filesystem;
    static std::atomic<int> ctr{9000};
    fs::path work = fs::temp_directory_path() /
                    ("clirun_" + std::to_string(ctr.fetch_add(1)) + "_" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::create_directories(work, ec);
    fs::path outF = work / "out.txt";
    fs::path errF = work / "err.txt";
    std::string cmd = TEST_CLI_BIN; // already a quoted literal via -DTEST_CLI_BIN="..."
    for (const auto& a : args)
        cmd += " " + shellQuote(a);
    cmd += " >" + shellQuote(outF.string()) + " 2>" + shellQuote(errF.string());
    int rc = std::system(cmd.c_str());
#ifdef _WIN32
    int code = rc;
#else
    int code = (rc == -1) ? -1 : WEXITSTATUS(rc);
#endif
    RunResult r{code, readFile(outF), readFile(errF)};
    fs::remove_all(work, ec);
    return r;
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// Hermetic config: every CLI invocation that can reach the daemon gets
// --config pointing at a fresh temp dir, so db/socket/pid never touch the
// real user locations (socket derives from the unique dbPath hash).
std::vector<std::string> withCfg(const std::string& tag, std::vector<std::string> rest) {
    namespace fs = std::filesystem;
    fs::path dir = tempDirPath(tag);
    fs::path cfg = dir / "config.json";
    {
        std::ofstream f(cfg, std::ios::binary);
        f << "{\"dbPath\": \"" << dir.generic_string() << "/library.db\"}";
    }
    rest.insert(rest.begin(), {"--config", cfg.generic_string()});
    return rest;
}

std::vector<std::string> withCfg(const std::string& tag, std::initializer_list<std::string> rest) {
    return withCfg(tag, std::vector<std::string>(rest));
}

} // namespace

TEST_CASE("cli help surfaces groups", "[cli]") {
    auto r = runCli({"--help"});
    REQUIRE(r.exitCode == 0);
    REQUIRE(contains(r.out, "SUBCOMMANDS"));
    for (const char* tok : {"start", "shutdown", "play", "status", "queue", "playlist",
                            "library", "tag", "history", "config", "device", "info"}) {
        INFO("missing token: " << tok);
        REQUIRE(contains(r.out, tok));
    }
}

TEST_CASE("cli group help lists leaf commands", "[cli]") {
    auto q = runCli({"queue", "--help"});
    REQUIRE(q.exitCode == 0);
    REQUIRE(contains(q.out, "shuffle"));
    REQUIRE(contains(q.out, "repeat"));
    auto l = runCli({"library", "scan", "--help"});
    REQUIRE(l.exitCode == 0);
    REQUIRE(contains(l.out, "sampled"));
    REQUIRE(contains(l.out, "full"));
}

TEST_CASE("cli version prints", "[cli]") {
    auto r = runCli({"--version"});
    REQUIRE(r.exitCode == 0);
    REQUIRE(!r.out.empty());
}

TEST_CASE("cli rejects unknown command", "[cli]") {
    auto r = runCli({"frobnicate"});
    REQUIRE(r.exitCode != 0);
    REQUIRE(contains(r.out + r.err, "frobnicate"));
}

TEST_CASE("cli rejects missing required arg", "[cli]") {
    auto r = runCli({"queue", "add"});
    REQUIRE(r.exitCode != 0);
}

TEST_CASE("cli seek validates time grammar", "[cli]") {
    auto bad = runCli(withCfg("gold_seek1", {"seek", "abc"}));
    REQUIRE(bad.exitCode == 1);
    REQUIRE(contains(bad.err, "invalid time"));
    REQUIRE(contains(bad.err, "mm:ss"));
    auto colons = runCli(withCfg("gold_seek2", {"seek", "1:2:3:4"}));
    REQUIRE(colons.exitCode == 1);
    REQUIRE(contains(colons.err, "too many colons"));
}

TEST_CASE("cli volume validates range", "[cli]") {
    auto bad = runCli(withCfg("gold_vol1", {"volume", "loud"}));
    REQUIRE(bad.exitCode == 1);
    REQUIRE(contains(bad.err, "invalid volume"));
    REQUIRE(contains(bad.err, "0-100"));
    auto range = runCli(withCfg("gold_vol2", {"volume", "101"}));
    REQUIRE(range.exitCode == 1);
    REQUIRE(contains(range.err, "out of range"));
}

TEST_CASE("cli shuffle/repeat echo valid modes", "[cli]") {
    auto s = runCli(withCfg("gold_shuf", {"queue", "shuffle", "bogus"}));
    REQUIRE(s.exitCode == 1);
    REQUIRE(contains(s.err, "invalid mode"));
    REQUIRE(contains(s.err, "on|off"));
    auto r = runCli(withCfg("gold_rep", {"queue", "repeat", "bogus"}));
    REQUIRE(r.exitCode == 1);
    REQUIRE(contains(r.err, "invalid mode"));
    REQUIRE(contains(r.err, "off|one|all"));
}

TEST_CASE("cli status watch validates interval", "[cli]") {
    auto r = runCli(withCfg("gold_int", {"status", "--watch", "--interval", "0"}));
    REQUIRE(r.exitCode == 1);
    REQUIRE(contains(r.err, "positive"));
}

TEST_CASE("cli enum options reject out-of-set values", "[cli]") {
    auto e = runCli(
        withCfg("gold_exp", {"playlist", "export", "--pid", "1", "--path", "x", "--format", "bogus"}));
    REQUIRE(e.exitCode != 0);
    auto t =
        runCli(withCfg("gold_tag", {"tag", "edit", "--id", "1", "--field", "bogus", "--value", "x"}));
    REQUIRE(t.exitCode != 0);
}

TEST_CASE("cli no-daemon errors point at start", "[cli]") {
    for (const char* sub : {"status"}) {
        auto r = runCli(withCfg("gold_nd1", {sub}));
        REQUIRE(r.exitCode == 1);
        REQUIRE(contains(r.err, "daemon not running"));
        REQUIRE(contains(r.err, "caudio start"));
    }
    auto q = runCli(withCfg("gold_nd2", {"queue", "list"}));
    REQUIRE(q.exitCode == 1);
    REQUIRE(contains(q.err, "daemon not running"));
    auto v = runCli(withCfg("gold_nd3", {"volume"}));
    REQUIRE(v.exitCode == 1);
    REQUIRE(contains(v.err, "daemon not running"));
}

TEST_CASE("cli preview missing file fails fast", "[cli]") {
    auto r = runCli(withCfg("gold_prev", {"preview", "no-such-file-xyz.wav"}));
    REQUIRE(r.exitCode == 1);
    REQUIRE(contains(r.err, "file not found"));
}

TEST_CASE("cli daemon lifecycle", "[cli]") {
    namespace fs = std::filesystem;
    fs::path dir = tempDirPath("gold_life");
    fs::path cfg = dir / "config.json";
    {
        std::ofstream f(cfg, std::ios::binary);
        f << "{\"dbPath\": \"" << dir.generic_string() << "/library.db\"}";
    }
    std::string cfgS = cfg.generic_string();
    auto run = [&](std::vector<std::string> a) {
        a.insert(a.begin(), {"--config", cfgS});
        return runCli(a);
    };
    auto start = run({"start"});
    REQUIRE(start.exitCode == 0);
    REQUIRE(contains(start.out, "started"));
    // Poll until the daemon answers (spawn is async).
    bool up = false;
    for (int i = 0; i < 150 && !up; ++i) {
        auto s = run({"status"});
        up = (s.exitCode == 0);
        if (!up)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    REQUIRE(up);
    auto js = run({"status", "--json"});
    REQUIRE(js.exitCode == 0);
    REQUIRE(contains(js.out, "\"state\""));
    auto vol = run({"volume"});
    REQUIRE(vol.exitCode == 0);
    REQUIRE(contains(vol.out, "Volume:"));
    auto set72 = run({"volume", "72"});
    REQUIRE(set72.exitCode == 0);
    auto vol2 = run({"volume"});
    REQUIRE(contains(vol2.out, "72%"));
    // One dummy audio file: scan tolerates missing metadata.
    fs::path music = dir / "music";
    std::error_code ec;
    fs::create_directories(music, ec);
    {
        std::ofstream f(music / "song.mp3", std::ios::binary);
        f << "dummy audio content for cli golden test";
    }
    auto scan = run({"library", "scan", "--path", music.generic_string()});
    REQUIRE(scan.exitCode == 0);
    auto stats = run({"library", "stats"});
    REQUIRE(stats.exitCode == 0);
    REQUIRE(contains(stats.out, "Tracks: 1"));
    auto add = run({"queue", "add", (music / "song.mp3").generic_string()});
    REQUIRE(add.exitCode == 0);
    auto ql = run({"queue", "list"});
    REQUIRE(ql.exitCode == 0);
    REQUIRE(contains(ql.out, "1 tracks"));
    auto qj = run({"queue", "list", "--json"});
    REQUIRE(qj.exitCode == 0);
    REQUIRE(contains(qj.out, "\"tracks\""));
    auto set = run({"config", "set", "cli_golden_key", "hello"});
    REQUIRE(set.exitCode == 0);
    REQUIRE(contains(set.out, "cli_golden_key = hello"));
    auto get = run({"config", "get", "cli_golden_key"});
    REQUIRE(get.exitCode == 0);
    REQUIRE(contains(get.out, "hello"));
    // Empty the queue, then play must fail fast without touching audio.
    auto clear = run({"queue", "clear"});
    REQUIRE(clear.exitCode == 0);
    REQUIRE(contains(clear.out, "cleared"));
    auto shuf = run({"queue", "shuffle", "on"});
    REQUIRE(shuf.exitCode == 0);
    REQUIRE(contains(shuf.out, "Shuffle: on"));
    auto play = run({"play"});
    REQUIRE(play.exitCode != 0);
    auto down = run({"shutdown"});
    REQUIRE(down.exitCode == 0);
    REQUIRE(contains(down.out, "stopped"));
    auto after = run({"status"});
    REQUIRE(after.exitCode == 1);
    REQUIRE(contains(after.err, "daemon not running"));
    fs::remove_all(dir, ec);
}

TEST_CASE("cli playback one-liners", "[cli]") {
    CAUDIO_SKIP_IF_NOAUDIO();
    namespace fs = std::filesystem;
    fs::path dir = tempDirPath("gold_play");
    fs::path cfg = dir / "config.json";
    {
        std::ofstream f(cfg, std::ios::binary);
        f << "{\"dbPath\": \"" << dir.generic_string() << "/library.db\"}";
    }
    std::string cfgS = cfg.generic_string();
    auto run = [&](std::vector<std::string> a) {
        a.insert(a.begin(), {"--config", cfgS});
        return runCli(a);
    };
    auto EpicFail = [&](const char* what) {
        std::error_code ec;
        fs::remove_all(dir, ec);
        FAIL(what);
    };
    if (run({"start"}).exitCode != 0)
        EpicFail("daemon did not start");
    bool up = false;
    for (int i = 0; i < 150 && !up; ++i) {
        up = (run({"status"}).exitCode == 0);
        if (!up)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!up)
        EpicFail("daemon never came up");
    auto dev = run({"device", "list", "--json"});
    if (!contains(dev.out, "\"devices\"") || contains(dev.out, "\"devices\": []")) {
        run({"shutdown"});
        std::error_code ec;
        fs::remove_all(dir, ec);
        SKIP("no audio device for playback one-liners");
    }
#ifdef TEST_DATA_DIR
    fs::path wav = fs::path(std::string(TEST_DATA_DIR)) / "sample.wav";
#else
    fs::path wav;
#endif
    if (wav.empty() || !fs::exists(wav)) {
        run({"shutdown"});
        std::error_code ec;
        fs::remove_all(dir, ec);
        SKIP("no fixture sample.wav");
    }
    REQUIRE(run({"library", "add", wav.generic_string()}).exitCode == 0);
    REQUIRE(run({"queue", "add", wav.generic_string()}).exitCode == 0);
    auto play = run({"play"});
    REQUIRE(play.exitCode == 0);
    REQUIRE(contains(play.out, "Playing "));
    REQUIRE(contains(play.out, "sample.wav"));
    auto pause = run({"pause"});
    REQUIRE(pause.exitCode == 0);
    REQUIRE(contains(pause.out, "Paused sample.wav at "));
    auto resume = run({"resume"});
    REQUIRE(resume.exitCode == 0);
    REQUIRE(contains(resume.out, "Resuming sample.wav from "));
    auto stop = run({"stop"});
    REQUIRE(stop.exitCode == 0);
    REQUIRE(contains(stop.out, "Stopped"));
    REQUIRE(run({"shutdown"}).exitCode == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
