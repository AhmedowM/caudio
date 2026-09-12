#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <filesystem>
#include <vector>
#include <variant>
#include <iostream>

#include "common.hpp"

import caudio.cli;
import caudio.service;
import caudio.client;
import caudio.engine;
import caudio.db;
import caudio.utils;

using namespace caudio::cli;
using namespace caudio::utils;
using namespace caudio::db;
using namespace caudio::engine;
using namespace caudio::service;
using namespace caudio::client;
using namespace caudio::test_helpers;

TEST_CASE("daemon start/stop", "[ipc][cli]") {
    std::string suffix = "roundtrip";
    std::filesystem::path dbPath = tempDbPath(suffix);
    std::error_code ec;
    std::filesystem::remove(dbPath, ec);
    std::filesystem::remove(dbPath.string() + "-wal", ec);
    std::filesystem::remove(dbPath.string() + "-shm", ec);

    ServiceConfig cfg;
    cfg.dbPath = dbPath;
    cfg.configPath = dbPath.parent_path() / ("caudio_test_" + suffix + ".json");
    std::filesystem::remove(cfg.configPath, ec);

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
    std::visit([&](auto&& v) {
        using U = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<U, Status>) {
            hasStatus = true;
            REQUIRE(v.state == PlaybackState::Stopped);
            REQUIRE(v.q_size == 0);
        }
    }, statusRes.value());
    REQUIRE(hasStatus);

    // Shutdown via new connection (Windows named pipes are single-use)
    auto clientRes2 = IpcClient::connect(cfg.dbPath);
    REQUIRE(clientRes2.has_value());
    auto client2 = std::move(clientRes2.value());
    auto shutdownRes = client2.send(Shutdown{});
    REQUIRE(shutdownRes.has_value());
    
    svcThread.request_stop();
    svcThread.join();

    // Verify cleanup
    std::filesystem::remove(dbPath, ec);
    std::filesystem::remove(dbPath.string() + "-wal", ec);
    std::filesystem::remove(dbPath.string() + "-shm", ec);
    std::filesystem::remove(cfg.configPath, ec);
}

TEST_CASE("command roundtrip - volume", "[ipc][cli]") {
    std::string suffix = "volume";
    std::filesystem::path dbPath = tempDbPath(suffix);
    std::error_code ec;
    std::filesystem::remove(dbPath, ec);
    std::filesystem::remove(dbPath.string() + "-wal", ec);
    std::filesystem::remove(dbPath.string() + "-shm", ec);

    ServiceConfig cfg;
    cfg.dbPath = dbPath;
    cfg.configPath = dbPath.parent_path() / ("caudio_test_" + suffix + ".json");
    std::filesystem::remove(cfg.configPath, ec);

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
    std::visit([&](auto&& v) {
        using U = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<U, VolumeInfo>) {
            hasVol = true;
            // Volume is stored as 0.0-1.0, input is 0-100
            REQUIRE(v.vol == 0.75f);
        }
    }, volRes.value());
    REQUIRE(hasVol);

    // Shutdown
    auto clientRes2 = IpcClient::connect(cfg.dbPath);
    REQUIRE(clientRes2.has_value());
    auto client2 = std::move(clientRes2.value());
    (void)client2.send(Shutdown{});
    
    svcThread.request_stop();
    svcThread.join();

    // Cleanup
    std::filesystem::remove(dbPath, ec);
    std::filesystem::remove(dbPath.string() + "-wal", ec);
    std::filesystem::remove(dbPath.string() + "-shm", ec);
    std::filesystem::remove(cfg.configPath, ec);
}

TEST_CASE("command roundtrip - status", "[ipc][cli]") {
    std::string suffix = "status";
    std::filesystem::path dbPath = tempDbPath(suffix);
    std::error_code ec;
    std::filesystem::remove(dbPath, ec);
    std::filesystem::remove(dbPath.string() + "-wal", ec);
    std::filesystem::remove(dbPath.string() + "-shm", ec);

    ServiceConfig cfg;
    cfg.dbPath = dbPath;
    cfg.configPath = dbPath.parent_path() / ("caudio_test_" + suffix + ".json");
    std::filesystem::remove(cfg.configPath, ec);

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
    std::visit([&](auto&& v) {
        using U = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<U, Status>) {
            hasStatus = true;
            REQUIRE(v.state == PlaybackState::Stopped);
            REQUIRE(v.q_size == 0);
        }
    }, statusRes.value());
    REQUIRE(hasStatus);

    // Shutdown
    auto clientRes2 = IpcClient::connect(cfg.dbPath);
    REQUIRE(clientRes2.has_value());
    auto client2 = std::move(clientRes2.value());
    (void)client2.send(Shutdown{});
    
    svcThread.request_stop();
    svcThread.join();

    // Cleanup
    std::filesystem::remove(dbPath, ec);
    std::filesystem::remove(dbPath.string() + "-wal", ec);
    std::filesystem::remove(dbPath.string() + "-shm", ec);
    std::filesystem::remove(cfg.configPath, ec);
}





