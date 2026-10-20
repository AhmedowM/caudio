#include <catch2/catch_test_macros.hpp>
#include <caudio/client/ipc_client.hpp>
#include <caudio/db/types.hpp>
#include <caudio/engine/types.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/protocol.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/service/service.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/json.hpp>
#include <chrono>
#include <common.hpp>
#include <filesystem>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
using namespace caudio::ipc;
using namespace caudio::utils;
using namespace caudio::db;
using namespace caudio::engine;
using namespace caudio::service;
using namespace caudio::client;
using namespace caudio::test_helpers;

namespace {

template <class... Ts>
constexpr bool allCommandAlternatives(std::variant<Ts...>*) {
    return (CommandAlternative<Ts> && ...);
}

static_assert(allCommandAlternatives(static_cast<Command*>(nullptr)));
static_assert(CommandType<const Play&>);
static_assert(!CommandAlternative<const Play>);

static_assert(std::is_same_v<caudio::ipc::DeviceInfo, caudio::types::DeviceInfo>);
static_assert(std::is_same_v<caudio::ipc::LibraryStatsData, caudio::types::LibraryStats>);
static_assert(
    std::is_same_v<caudio::ipc::LibraryStatsDetailedData, caudio::types::LibraryStatsDetailed>);
static_assert(
    std::is_same_v<caudio::db::LibraryStatsDetailedData, caudio::types::LibraryStatsDetailed>);
static_assert(std::is_same_v<caudio::ipc::HistoryEntry, caudio::types::HistoryEntry>);
static_assert(std::is_same_v<caudio::ipc::BookmarkEntry, caudio::types::Bookmark>);

} // namespace

TEST_CASE("daemon start/stop", "[ipc][cli]") {
    std::string suffix = "roundtrip";
    std::filesystem::path dir = tempDirPath("ipc_" + suffix);
    std::filesystem::path dbPath = dir / "library.db";
    std::error_code ec;

    ServiceConfig cfg;
    cfg.dbPath = dbPath;
    cfg.configPath = dir / "config.json";

    // Start daemon
    auto svcRes = Service::create(cfg);
    REQUIRE(svcRes.has_value());
    auto svc = std::move(svcRes.value());

    std::jthread svcThread([&](std::stop_token st) { (void)svc->run(st); });
    // wait for socket ready
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Connect client and send one command
    auto clientRes = IpcClient::connect(cfg.dbPath);
    REQUIRE(clientRes.has_value());
    auto client = std::move(clientRes.value());

    // Test status (no tracks yet)
    auto statusRes = client.send(StatusReq{});
    REQUIRE(statusRes.has_value());
    bool hasStatus = false;
    std::visit(
        [&](auto&& v) {
            using U = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<U, Status>) {
                hasStatus = true;
                REQUIRE(v.state == caudio::types::PlaybackState::Stopped);
                REQUIRE(v.q_size == 0);
            }
        },
        statusRes.value());
    REQUIRE(hasStatus);

    // Shutdown via new connection (Windows named pipes are single-use)
    auto clientRes2 = IpcClient::connect(cfg.dbPath);
    REQUIRE(clientRes2.has_value());
    auto client2 = std::move(clientRes2.value());
    auto shutdownRes = client2.send(Shutdown{});
    REQUIRE(shutdownRes.has_value());

    svcThread.request_stop();
    svcThread.join();
    svc.reset();

    // Verify cleanup
    std::filesystem::remove(dbPath, ec);
    std::filesystem::remove(dbPath.string() + "-wal", ec);
    std::filesystem::remove(dbPath.string() + "-shm", ec);
    std::filesystem::remove(cfg.configPath, ec);
}

TEST_CASE("bookmark protocol roundtrip", "[ipc]") {
    Command add{BookmarkAdd{83000, "note", 42}};
    auto addJson = toJson(add);
    auto addBack = commandFromJson(addJson);
    REQUIRE(addBack.has_value());
    auto* addVal = std::get_if<BookmarkAdd>(&*addBack);
    REQUIRE(addVal != nullptr);
    CHECK(addVal->position_ms == 83000);
    CHECK(addVal->note == "note");
    CHECK(addVal->track_id == 42);

    Command list{BookmarkList{std::nullopt}};
    auto listJson = toJson(list);
    auto listBack = commandFromJson(listJson);
    REQUIRE(listBack.has_value());
    auto* listVal = std::get_if<BookmarkList>(&*listBack);
    REQUIRE(listVal != nullptr);
    CHECK(!listVal->track_id.has_value());

    Result res{Bookmarks{{
        BookmarkEntry{1, 42, 83000, "note", 1700},
    }}};
    auto resJson = toJson(res);
    auto resBack = resultFromJson(resJson);
    REQUIRE(resBack.has_value());
    auto* marks = std::get_if<Bookmarks>(&*resBack);
    REQUIRE(marks != nullptr);
    REQUIRE(marks->entries.size() == 1);
    CHECK(marks->entries[0].id == 1);
    CHECK(marks->entries[0].track_id == 42);
    CHECK(marks->entries[0].position_ms == 83000);
    CHECK(marks->entries[0].note == "note");
    CHECK(marks->entries[0].created == 1700);
}

TEST_CASE("history protocol roundtrip", "[ipc]") {
    Result res{History{{
        HistoryEntry{1, 42, 1700000000000, 1700000000240, 240000, 100.0, 1, "Song", "Artist",
                     "/music/song.mp3", 240.0},
    }}};
    auto resJson = toJson(res);
    auto resBack = resultFromJson(resJson);
    REQUIRE(resBack.has_value());
    auto* history = std::get_if<History>(&*resBack);
    REQUIRE(history != nullptr);
    REQUIRE(history->entries.size() == 1);
    const auto& entry = history->entries[0];
    CHECK(entry.id == 1);
    CHECK(entry.track_id == 42);
    CHECK(entry.started_at == 1700000000000);
    CHECK(entry.completed_at == 1700000000240);
    CHECK(entry.position_ms == 240000);
    CHECK(entry.completion_pct == 100.0);
    CHECK(entry.queue_id == 1);
    CHECK(entry.title == "Song");
    CHECK(entry.artist == "Artist");
    CHECK(entry.path == "/music/song.mp3");
    CHECK(entry.duration == 240.0);
}

TEST_CASE("devices protocol roundtrip", "[ipc]") {
    Result res{Devices{{
        DeviceInfo{"hw:0,0", "Built-in Audio", true},
        DeviceInfo{"hw:1,0", "USB Headset", false},
    }}};
    auto resJson = toJson(res);
    REQUIRE(resJson["type"].snapshot().get<std::string>() == "Devices");
    auto resBack = resultFromJson(resJson);
    REQUIRE(resBack.has_value());
    auto* devices = std::get_if<Devices>(&*resBack);
    REQUIRE(devices != nullptr);
    REQUIRE(devices->devices.size() == 2);
    CHECK(devices->devices[0].id == "hw:0,0");
    CHECK(devices->devices[0].name == "Built-in Audio");
    CHECK(devices->devices[0].isDefault);
    CHECK(devices->devices[1].id == "hw:1,0");
    CHECK_FALSE(devices->devices[1].isDefault);
}

TEST_CASE("library stats detailed protocol roundtrip", "[ipc]") {
    caudio::types::Track track{};
    track.id = 7;
    track.path = "/music/song.mp3";
    track.title = "Song";
    track.duration = 240.0;
    Result res{caudio::types::LibraryStatsDetailed{100, 5, 20, 36000000, 7200000, {track}}};
    auto resJson = toJson(res);
    REQUIRE(resJson["type"].snapshot().get<std::string>() == "LibraryStatsDetailed");
    auto resBack = resultFromJson(resJson);
    REQUIRE(resBack.has_value());
    auto* stats = std::get_if<caudio::types::LibraryStatsDetailed>(&*resBack);
    REQUIRE(stats != nullptr);
    CHECK(stats->tracks == 100);
    CHECK(stats->queues == 5);
    CHECK(stats->playlists == 20);
    CHECK(stats->total_duration_ms == 36000000);
    CHECK(stats->total_play_time_ms == 7200000);
    REQUIRE(stats->most_played.size() == 1);
    CHECK(stats->most_played[0].id == 7);
    CHECK(stats->most_played[0].title == "Song");
}

TEST_CASE("command roundtrip - volume", "[ipc][cli]") {
    std::string suffix = "volume";
    std::filesystem::path dir = tempDirPath("ipc_" + suffix);
    std::filesystem::path dbPath = dir / "library.db";
    std::error_code ec;

    ServiceConfig cfg;
    cfg.dbPath = dbPath;
    cfg.configPath = dir / "config.json";

    // Start daemon
    auto svcRes = Service::create(cfg);
    REQUIRE(svcRes.has_value());
    auto svc = std::move(svcRes.value());

    std::jthread svcThread([&](std::stop_token st) { (void)svc->run(st); });
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Connect and send volume command
    auto clientRes = IpcClient::connect(cfg.dbPath);
    REQUIRE(clientRes.has_value());
    auto client = std::move(clientRes.value());

    // Test volume
    auto volRes = client.send(VolumeSet{.level = 75.0f});
    REQUIRE(volRes.has_value());
    bool hasVol = false;
    std::visit(
        [&](auto&& v) {
            using U = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<U, VolumeInfo>) {
                hasVol = true;
                // Volume is stored as 0.0-1.0, input is 0-100
                REQUIRE(v.vol == 0.75f);
            }
        },
        volRes.value());
    REQUIRE(hasVol);

    // Shutdown
    auto clientRes2 = IpcClient::connect(cfg.dbPath);
    REQUIRE(clientRes2.has_value());
    auto client2 = std::move(clientRes2.value());
    (void)client2.send(Shutdown{});

    svcThread.request_stop();
    svcThread.join();
    svc.reset();

    // Cleanup
    std::filesystem::remove(dbPath, ec);
    std::filesystem::remove(dbPath.string() + "-wal", ec);
    std::filesystem::remove(dbPath.string() + "-shm", ec);
    std::filesystem::remove(cfg.configPath, ec);
}

TEST_CASE("command roundtrip - status", "[ipc][cli]") {
    std::string suffix = "status";
    std::filesystem::path dir = tempDirPath("ipc_" + suffix);
    std::filesystem::path dbPath = dir / "library.db";
    std::error_code ec;

    ServiceConfig cfg;
    cfg.dbPath = dbPath;
    cfg.configPath = dir / "config.json";

    // Start daemon
    auto svcRes = Service::create(cfg);
    REQUIRE(svcRes.has_value());
    auto svc = std::move(svcRes.value());

    std::jthread svcThread([&](std::stop_token st) { (void)svc->run(st); });
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Connect and send status command
    auto clientRes = IpcClient::connect(cfg.dbPath);
    REQUIRE(clientRes.has_value());
    auto client = std::move(clientRes.value());

    // Test status
    auto statusRes = client.send(StatusReq{});
    REQUIRE(statusRes.has_value());
    bool hasStatus = false;
    std::visit(
        [&](auto&& v) {
            using U = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<U, Status>) {
                hasStatus = true;
                REQUIRE(v.state == caudio::types::PlaybackState::Stopped);
                REQUIRE(v.q_size == 0);
            }
        },
        statusRes.value());
    REQUIRE(hasStatus);

    // Shutdown
    auto clientRes2 = IpcClient::connect(cfg.dbPath);
    REQUIRE(clientRes2.has_value());
    auto client2 = std::move(clientRes2.value());
    (void)client2.send(Shutdown{});

    svcThread.request_stop();
    svcThread.join();
    svc.reset();

    // Cleanup
    std::filesystem::remove(dbPath, ec);
    std::filesystem::remove(dbPath.string() + "-wal", ec);
    std::filesystem::remove(dbPath.string() + "-shm", ec);
    std::filesystem::remove(cfg.configPath, ec);
}
