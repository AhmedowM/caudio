module;
#include <CLI/CLI.hpp>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>
#include "caudio/version.hpp"

export module caudio.app:core;

import caudio.utils;
import caudio.cli;
import caudio.client;
import caudio.service;
import caudio.engine;
import caudio.player;

export namespace caudio::app {

namespace detail {
inline std::expected<double, caudio::utils::Error> parseTime(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    if (s.empty()) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "empty time")};
    size_t colonCount = 0;
    for (char c : s) if (c == ':') ++colonCount;
    if (colonCount > 0) {
        double parts[3] = {0,0,0};
        std::string_view rem = s;
        int idx = 0;
        while (idx < 3) {
            size_t nxt = rem.find(':');
            std::string_view tok = (nxt == std::string_view::npos) ? rem : rem.substr(0, nxt);
            if (tok.empty()) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "empty time component")};
            bool isLast = (nxt == std::string_view::npos);
            if (isLast) {
                double v = 0;
                auto r = std::from_chars(tok.data(), tok.data() + tok.size(), v);
                if (r.ec != std::errc{} || r.ptr != tok.data() + tok.size()) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "invalid time component")};
                parts[idx++] = v;
                break;
            } else {
                int iv = 0;
                auto r = std::from_chars(tok.data(), tok.data() + tok.size(), iv);
                if (r.ec != std::errc{} || r.ptr != tok.data() + tok.size()) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "invalid time component")};
                if (iv < 0) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "negative time component")};
                parts[idx++] = static_cast<double>(iv);
                rem.remove_prefix(nxt + 1);
            }
        }
        double total = 0;
        if (colonCount == 1) total = parts[0]*60.0 + parts[1];
        else if (colonCount == 2) total = parts[0]*3600.0 + parts[1]*60.0 + parts[2];
        else return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "too many colons")};
        if (!std::isfinite(total) || total < 0) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "invalid time")};
        return total;
    } else {
        double v = 0;
        auto r = std::from_chars(s.data(), s.data() + s.size(), v);
        if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "invalid time")};
        if (!std::isfinite(v) || v < 0) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "invalid time")};
        return v;
    }
}
inline std::expected<double, caudio::utils::Error> parseSeek(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    if (s.empty()) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "empty seek")};
    bool relative = false; bool neg = false; std::string_view core = s;
    if (core.front() == '+' || core.front() == '-') { relative = true; neg = (core.front() == '-'); core.remove_prefix(1); if (core.empty()) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "missing seek value")}; }
    auto t = parseTime(core); if (!t) return std::unexpected{t.error()}; double v = *t; if (relative) { if (neg) v = -v; return v; } return v;
}
inline std::expected<caudio::cli::VolumeSet, caudio::utils::Error> parseVolume(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    caudio::cli::VolumeSet vs{}; if (s.empty()) return vs; if (s == "mute") { vs.mute = true; return vs; } if (s == "unmute") { vs.mute = false; return vs; }
    if (s.front() == '+' || s.front() == '-') { bool n = s.front() == '-'; std::string_view num = s.substr(1); if (num.empty()) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "invalid delta")}; int iv=0; auto r=std::from_chars(num.data(), num.data()+num.size(), iv); if (r.ec!=std::errc{}|| r.ptr!=num.data()+num.size()) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "invalid delta")}; if (n) iv=-iv; vs.deltaPct=iv; return vs; }
    int iv=0; auto r=std::from_chars(s.data(), s.data()+s.size(), iv); if (r.ec!=std::errc{}|| r.ptr!=s.data()+s.size()) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg, "invalid volume")}; if (iv<0||iv>100) return std::unexpected{caudio::utils::makeError(caudio::utils::Result::InvalidArg,"volume out of range 0-100")}; vs.level=static_cast<float>(iv); return vs;
}
inline std::chrono::duration<double> parseDuration(std::string_view s){ auto t=parseTime(s); if(!t) return std::chrono::duration<double>{0}; return std::chrono::duration<double>{*t}; }
} // namespace detail
using detail::parseTime;
using detail::parseSeek;
using detail::parseVolume;

class App {
public:
    explicit App(caudio::cli::Config cfg) : config_(std::move(cfg)), cli_("caudio - terminal player") {
        cli_.set_version_flag("--version", std::string(caudio::kVersion));
    }
    int run(int argc, char** argv);
private:
    int handleStart(bool foreground);
    int handlePreview(const std::string& file);
    caudio::cli::Config config_{};
    CLI::App cli_{"caudio - terminal player"};
};

inline int App::handleStart(bool foreground) {
    auto conn = caudio::client::IpcClient::connect(config_.dbPath);
    if (conn) { std::cout << std::format("daemon already running at {}\n", config_.socketPath.generic_string()); return 0; }
    caudio::service::ServiceConfig scfg; scfg.dbPath = config_.dbPath; scfg.socketPath = config_.socketPath; scfg.logLevel = config_.logLevel;
    if (foreground) {
        auto svc = caudio::service::Service::create(scfg);
        if (!svc) { std::cerr << std::format("start failed: {} {}\n", std::to_string(std::to_underlying(svc.error().code)), svc.error().message); return 1; }
        std::cout << std::format("starting daemon foreground at {}\n", config_.socketPath.generic_string());
        std::stop_source ss; auto res = svc.value()->run(ss.get_token()); if (!res) { std::cerr << std::format("daemon error: {}\n", res.error().message); return 1; } return 0;
    } else {
        auto svc = caudio::service::Service::create(scfg);
        if (!svc) { std::cerr << std::format("start failed: {} {}\n", std::to_string(std::to_underlying(svc.error().code)), svc.error().message); return 1; }
        auto svcPtr = std::shared_ptr<caudio::service::Service>(std::move(svc.value()));
        static std::vector<std::shared_ptr<std::jthread>> bg; static std::mutex bgMtx;
        auto thr = std::make_shared<std::jthread>([svcPtr](std::stop_token st) mutable { (void)svcPtr->run(st); });
        { std::lock_guard<std::mutex> lk(bgMtx); bg.push_back(thr); }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto conn2 = caudio::client::IpcClient::connect(config_.dbPath);
        if (!conn2) { std::cerr << std::format("daemon start failed, socket not reachable at {}\n", config_.socketPath.generic_string()); return 1; }
        std::cout << std::format("daemon started at {}\n", config_.socketPath.generic_string()); return 0;
    }
}
inline int App::handlePreview(const std::string& file) {
    std::filesystem::path p{file}; std::error_code ec; if (!std::filesystem::exists(p, ec)) { std::cerr << std::format("preview: file not found {}\n", file); return 1; }
    auto playerRes = caudio::player::Player::create(); if (!playerRes) { std::cerr << std::format("preview: player create failed {} {}\n", std::to_string(std::to_underlying(playerRes.error().code)), playerRes.error().message); return 1; }
    auto& player = *playerRes.value(); auto openRes = player.open(file); if (!openRes) { std::cerr << std::format("preview: open failed {} {}\n", std::to_string(std::to_underlying(openRes.error().code)), openRes.error().message); return 1; }
    auto playRes = player.play(); if (!playRes) { std::cerr << std::format("preview: play failed {} {}\n", std::to_string(std::to_underlying(playRes.error().code)), playRes.error().message); return 1; }
    std::cout << std::format("preview playing {}\n", file); std::span<const std::byte> dummy; (void)dummy;
    while (player.state() == caudio::player::State::Playing) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
    std::cout << std::format("preview done\n"); return 0;
}
inline int App::run(int argc, char** argv) {
    std::string dbPathStr; std::string configPathStr; std::string logLevelStr; std::string deviceStr;
    cli_.add_option("--db-path", dbPathStr, "Database file");
    cli_.add_option("--config", configPathStr, "Config file");
    cli_.add_option("--log-level", logLevelStr, "trace|debug|info|warn|error");
    cli_.add_option("--device", deviceStr, "Audio output device");
    bool fg=false; auto* startCmd=cli_.add_subcommand("start","Start daemon"); startCmd->add_flag("--foreground",fg,"Run in foreground");
    auto* shutdownCmd=cli_.add_subcommand("shutdown","Stop daemon");
    auto* playCmd=cli_.add_subcommand("play","Play current queue");
    auto* pauseCmd=cli_.add_subcommand("pause","Pause playback");
    auto* resumeCmd=cli_.add_subcommand("resume","Resume playback");
    auto* restartCmd=cli_.add_subcommand("restart","Restart current track");
    auto* stopCmd=cli_.add_subcommand("stop","Stop playback");
    auto* nextCmd=cli_.add_subcommand("next","Next track");
    auto* prevCmd=cli_.add_subcommand("prev","Prev track");
    std::string seekStr; auto* seekCmd=cli_.add_subcommand("seek","Seek to position"); seekCmd->add_option("time",seekStr,"mm:ss or seconds or +N/-N")->required();
    bool jsonFlag=false; auto* statusCmd=cli_.add_subcommand("status","Show status"); statusCmd->add_flag("--json",jsonFlag,"JSON output");
    std::string volumeArg; auto* volumeCmd=cli_.add_subcommand("volume","Get/set volume"); volumeCmd->add_option("level",volumeArg,"0-100|+N|-N|mute|unmute");
    auto* queueCmd=cli_.add_subcommand("queue","Queue operations");
    bool qJson=false; auto* qList=queueCmd->add_subcommand("list","List queue tracks"); qList->add_flag("--json",qJson,"JSON output");
    auto* qQueues=queueCmd->add_subcommand("queues","List all queues");
    std::int64_t qSwitchId=0; auto* qSwitch=queueCmd->add_subcommand("switch","Switch active queue"); qSwitch->add_option("qid",qSwitchId,"Queue id")->required();
    std::string qAddQuery; bool qAddSearch=false; auto* qAdd=queueCmd->add_subcommand("add","Add to queue"); qAdd->add_option("query",qAddQuery,"id|path|query")->required(); qAdd->add_flag("--search",qAddSearch,"Force FTS search");
    std::string qRemoveId; auto* qRemove=queueCmd->add_subcommand("remove","Remove from queue"); qRemove->add_option("id",qRemoveId,"index or id")->required();
    std::size_t qFrom=0,qTo=0; auto* qMove=queueCmd->add_subcommand("move","Move within queue"); qMove->add_option("from",qFrom,"from index")->required(); qMove->add_option("to",qTo,"to index")->required();
    auto* qClear=queueCmd->add_subcommand("clear","Clear queue");
    std::string qShuffleArg; auto* qShuffle=queueCmd->add_subcommand("shuffle","Set shuffle"); qShuffle->add_option("mode",qShuffleArg,"on|off");
    std::string qRepeatArg; auto* qRepeat=queueCmd->add_subcommand("repeat","Set repeat"); qRepeat->add_option("mode",qRepeatArg,"off|one|all");
    auto* plCmd=cli_.add_subcommand("playlist","Playlist operations");
    bool plJson=false; auto* plList=plCmd->add_subcommand("list","List playlists"); plList->add_flag("--json",plJson,"JSON output");
    std::int64_t plTracksPid=0; auto* plTracks=plCmd->add_subcommand("tracks","Tracks in playlist"); plTracks->add_option("pid",plTracksPid,"Playlist id")->required();
    std::int64_t plLoadPid=0; bool plLoadPlay=false; auto* plLoad=plCmd->add_subcommand("load","Load playlist into queue"); plLoad->add_option("pid",plLoadPid,"Playlist id")->required(); plLoad->add_flag("--play",plLoadPlay,"Play after load");
    std::string plSaveName; std::int64_t plSaveQid=0; auto* plSave=plCmd->add_subcommand("save","Save queue as playlist"); plSave->add_option("name",plSaveName,"Playlist name")->required(); plSave->add_option("--queue",plSaveQid,"Queue id");
    std::int64_t plDeletePid=0; auto* plDelete=plCmd->add_subcommand("delete","Delete playlist"); plDelete->add_option("pid",plDeletePid,"Playlist id")->required();
    auto* libCmd=cli_.add_subcommand("library","Library operations");
    std::string libScanPath; std::string libScanMode="sampled"; auto* libScan=libCmd->add_subcommand("scan","Scan library"); libScan->add_option("--path",libScanPath,"Scan path"); libScan->add_option("--mode",libScanMode,"sampled|full");
    std::string libSearchQuery; int libSearchLimit=50; bool libSearchJson=false; auto* libSearch=libCmd->add_subcommand("search","Search library"); libSearch->add_option("query",libSearchQuery,"FTS query")->required(); libSearch->add_option("--limit",libSearchLimit,"Limit"); libSearch->add_flag("--json",libSearchJson,"JSON output");
    bool libStatsJson=false; auto* libStats=libCmd->add_subcommand("stats","Library stats"); libStats->add_flag("--json",libStatsJson,"JSON output");
    std::string previewFile; auto* previewCmd=cli_.add_subcommand("preview","Preview file (ephemeral)"); previewCmd->add_option("file",previewFile,"File path")->required();
    auto* tuiCmd=cli_.add_subcommand("tui","Launch TUI");
    auto* cfgCmd=cli_.add_subcommand("config","Config operations");
    std::string cfgGetKey; auto* cfgGet=cfgCmd->add_subcommand("get","Get config value"); cfgGet->add_option("key",cfgGetKey,"Key")->required();
    std::string cfgSetKey,cfgSetVal; auto* cfgSet=cfgCmd->add_subcommand("set","Set config value"); cfgSet->add_option("key",cfgSetKey,"Key")->required(); cfgSet->add_option("value",cfgSetVal,"Value")->required();
    bool cfgListJson=false; auto* cfgList=cfgCmd->add_subcommand("list","List config"); cfgList->add_flag("--json",cfgListJson,"JSON output");
    std::string cfgExportPath; auto* cfgExport=cfgCmd->add_subcommand("export","Export config"); cfgExport->add_option("path",cfgExportPath,"Path")->required();
    std::string cfgImportPath; auto* cfgImport=cfgCmd->add_subcommand("import","Import config"); cfgImport->add_option("path",cfgImportPath,"Path")->required();
    try { cli_.parse(argc, argv); } catch (const CLI::ParseError& e) { return cli_.exit(e); }
    if (!dbPathStr.empty()) config_.dbPath = std::filesystem::path(dbPathStr);
    if (!configPathStr.empty()) { config_.configPath = std::filesystem::path(configPathStr); auto loaded = caudio::cli::loadConfig(config_.configPath); if (loaded) { if (dbPathStr.empty()) config_.dbPath = loaded->dbPath; config_.device = loaded->device; config_.logLevel = loaded->logLevel; if (!loaded->socketPath.empty()) config_.socketPath = loaded->socketPath; } }
    if (!deviceStr.empty()) config_.device = deviceStr;
    if (!logLevelStr.empty()) { if (logLevelStr=="trace") config_.logLevel=0; else if (logLevelStr=="debug") config_.logLevel=1; else if (logLevelStr=="info") config_.logLevel=2; else if (logLevelStr=="warn") config_.logLevel=3; else if (logLevelStr=="error") config_.logLevel=4; }
    if (config_.socketPath.empty() && !config_.dbPath.empty()) { auto sp = caudio::service::socketPathFor(config_.dbPath); if (sp) config_.socketPath = std::filesystem::path(*sp); }
    std::span<char> dummySpan; (void)dummySpan; std::error_code ec; (void)std::filesystem::exists(config_.dbPath, ec);
    auto sendViaClient = [&](const caudio::cli::Command& cmd, bool asJson)->int {
        caudio::client::Client client{config_.dbPath}; auto timeout=std::chrono::milliseconds{2000}; auto res=client.send(cmd, timeout); caudio::client::OutputFormatter fmt{asJson};
        if (!res) { caudio::cli::Result errRes{res.error()}; fmt.print(errRes, std::cerr); return 1; }
        if (std::holds_alternative<caudio::utils::Error>(*res)) { fmt.print(*res, std::cerr); return 1; }
        fmt.print(*res, std::cout); return 0;
    };
    if (startCmd->parsed()) return handleStart(fg);
    if (shutdownCmd->parsed()) { caudio::cli::Command cmd{caudio::cli::Shutdown{}}; return sendViaClient(cmd,false); }
    if (playCmd->parsed()) { caudio::cli::Command cmd{caudio::cli::Play{}}; return sendViaClient(cmd,false); }
    if (pauseCmd->parsed()) { caudio::cli::Command cmd{caudio::cli::Pause{}}; return sendViaClient(cmd,false); }
    if (resumeCmd->parsed()) { caudio::cli::Command cmd{caudio::cli::Resume{}}; return sendViaClient(cmd,false); }
    if (restartCmd->parsed()) { caudio::cli::Command cmd{caudio::cli::Restart{}}; return sendViaClient(cmd,false); }
    if (stopCmd->parsed()) { caudio::cli::Command cmd{caudio::cli::Stop{}}; return sendViaClient(cmd,false); }
    if (nextCmd->parsed()) { caudio::cli::Command cmd{caudio::cli::Next{}}; return sendViaClient(cmd,false); }
    if (prevCmd->parsed()) { caudio::cli::Command cmd{caudio::cli::Prev{}}; return sendViaClient(cmd,false); }
    if (seekCmd->parsed()) {
        auto parsed = detail::parseSeek(seekStr); if (!parsed) { std::cerr << std::format("seek: {}\n", parsed.error().message); return 1; }
        double target=*parsed; bool isRelative=!seekStr.empty() && (seekStr.front()=='+'||seekStr.front()=='-');
        if (isRelative) { caudio::client::Client client{config_.dbPath}; auto sres=client.send(caudio::cli::Command{caudio::cli::StatusReq{}}); double pos=0; bool hasPos=false; if (sres) { if (auto* ps = std::get_if<caudio::cli::Status>(&*sres)) { pos = ps->pos; hasPos=true; } } if (hasPos) { target=pos+target; if(target<0) target=0; } else { if(target<0) target=0; } }
        caudio::cli::Command cmd{caudio::cli::Seek{target}}; return sendViaClient(cmd,false);
    }
    if (statusCmd->parsed()) { caudio::cli::Command cmd{caudio::cli::StatusReq{}}; return sendViaClient(cmd, jsonFlag); }
    if (volumeCmd->parsed()) { auto pv=detail::parseVolume(volumeArg); if(!pv){ std::cerr<<std::format("volume: {}\n",pv.error().message); return 1; } caudio::cli::Command cmd{*pv}; return sendViaClient(cmd,false); }
    if (queueCmd->parsed()) {
        if (qList->parsed()) { caudio::cli::Command cmd{caudio::cli::QueueList{}}; return sendViaClient(cmd,qJson); }
        if (qQueues->parsed()) { caudio::cli::Command cmd{caudio::cli::QueueQueues{}}; return sendViaClient(cmd,false); }
        if (qSwitch->parsed()) { caudio::cli::Command cmd{caudio::cli::QueueSwitch{qSwitchId}}; return sendViaClient(cmd,false); }
        if (qAdd->parsed()) { caudio::cli::Command cmd{caudio::cli::QueueAdd{qAddQuery,qAddSearch}}; return sendViaClient(cmd,false); }
        if (qRemove->parsed()) { caudio::cli::Command cmd{caudio::cli::QueueRemove{qRemoveId}}; return sendViaClient(cmd,false); }
        if (qMove->parsed()) { caudio::cli::Command cmd{caudio::cli::QueueMove{qFrom,qTo}}; return sendViaClient(cmd,false); }
        if (qClear->parsed()) { caudio::cli::Command cmd{caudio::cli::QueueClear{}}; return sendViaClient(cmd,false); }
        if (qShuffle->parsed()) { std::optional<bool> on; if(qShuffleArg=="on") on=true; else if(qShuffleArg=="off") on=false; else if(!qShuffleArg.empty()){ std::cerr<<std::format("shuffle: invalid mode {}\n",qShuffleArg); return 1; } caudio::cli::Command cmd{caudio::cli::QueueShuffle{on}}; return sendViaClient(cmd,false); }
        if (qRepeat->parsed()) { std::optional<caudio::engine::RepeatMode> m; if(qRepeatArg=="off") m=caudio::engine::RepeatMode::Off; else if(qRepeatArg=="one") m=caudio::engine::RepeatMode::One; else if(qRepeatArg=="all") m=caudio::engine::RepeatMode::Queue; else if(!qRepeatArg.empty()){ std::cerr<<std::format("repeat: invalid mode {}\n",qRepeatArg); return 1; } caudio::cli::Command cmd{caudio::cli::QueueRepeat{m}}; return sendViaClient(cmd,false); }
        std::cout << queueCmd->help() << "\n"; return 0;
    }
    if (plCmd->parsed()) {
        if (plList->parsed()) { caudio::cli::Command cmd{caudio::cli::PlaylistList{}}; return sendViaClient(cmd,plJson); }
        if (plTracks->parsed()) { caudio::cli::Command cmd{caudio::cli::PlaylistTracks{plTracksPid}}; return sendViaClient(cmd,false); }
        if (plLoad->parsed()) { caudio::cli::Command cmd{caudio::cli::PlaylistLoad{plLoadPid,plLoadPlay}}; return sendViaClient(cmd,false); }
        if (plSave->parsed()) { std::optional<std::int64_t> qid; if(plSave->get_option("--queue")->count()>0) qid=plSaveQid; caudio::cli::Command cmd{caudio::cli::PlaylistSave{plSaveName,qid}}; return sendViaClient(cmd,false); }
        if (plDelete->parsed()) { caudio::cli::Command cmd{caudio::cli::PlaylistDelete{plDeletePid}}; return sendViaClient(cmd,false); }
        std::cout << plCmd->help() << "\n"; return 0;
    }
    if (libCmd->parsed()) {
        if (libScan->parsed()) { std::optional<std::string> p; if(!libScanPath.empty()) p=libScanPath; caudio::cli::Command cmd{caudio::cli::LibraryScan{p,libScanMode}}; return sendViaClient(cmd,false); }
        if (libSearch->parsed()) { caudio::cli::Command cmd{caudio::cli::LibrarySearch{libSearchQuery,libSearchLimit}}; return sendViaClient(cmd,libSearchJson); }
        if (libStats->parsed()) { caudio::cli::Command cmd{caudio::cli::LibraryStats{}}; return sendViaClient(cmd,libStatsJson); }
        std::cout << libCmd->help() << "\n"; return 0;
    }
    if (previewCmd->parsed()) return handlePreview(previewFile);
    if (tuiCmd->parsed()) { (void)handleStart(false); std::cout << std::format("tui: not implemented (daemon ensured)\n"); return 0; }
    if (cfgCmd->parsed()) {
        if (cfgGet->parsed()) { caudio::cli::Command cmd{caudio::cli::ConfigGet{cfgGetKey}}; return sendViaClient(cmd,false); }
        if (cfgSet->parsed()) { caudio::cli::Command cmd{caudio::cli::ConfigSet{cfgSetKey,cfgSetVal}}; return sendViaClient(cmd,false); }
        if (cfgList->parsed()) { caudio::cli::Command cmd{caudio::cli::ConfigList{}}; return sendViaClient(cmd,cfgListJson); }
        if (cfgExport->parsed()) { caudio::cli::Command cmd{caudio::cli::ConfigExport{cfgExportPath}}; return sendViaClient(cmd,false); }
        if (cfgImport->parsed()) { caudio::cli::Command cmd{caudio::cli::ConfigImport{cfgImportPath}}; return sendViaClient(cmd,false); }
        std::cout << cfgCmd->help() << "\n"; return 0;
    }
    std::cout << cli_.help() << "\n"; return 0;
}

} // namespace caudio::app
