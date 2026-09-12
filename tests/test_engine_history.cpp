#include <sqlite3.h>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <thread>

#include "common.hpp"

import caudio.db;
import caudio.engine;
import caudio.utils;

using namespace caudio::db;
using namespace caudio::engine;
using namespace caudio::utils;
using namespace caudio::test_helpers;

TEST_CASE("shouldMarkPlayed 60pct 90s thresholds", "[engine_history]") {
    // detail::shouldMarkPlayedEx is in engine namespace detail
    using caudio::engine::detail::shouldMarkPlayedEx;
    // duration 100s, pos 59 -> not marked (59%)
    REQUIRE(shouldMarkPlayedEx(100.0, 59.0, false, 60, 90) == false);
    // 61% true
    REQUIRE(shouldMarkPlayedEx(100.0, 61.0, false, 60, 90) == true);
    // already marked -> false even if threshold met
    REQUIRE(shouldMarkPlayedEx(100.0, 80.0, true, 60, 90) == false);
    // secs threshold: pos 91 with duration long and pct not met but secs met
    REQUIRE(shouldMarkPlayedEx(1000.0, 91.0, false, 60, 90) == true);
    // pct 60 with duration 0 -> only secs matters; pos 90 true via secs
    REQUIRE(shouldMarkPlayedEx(0.0, 90.0, false, 60, 90) == true);
    // short duration 10s, pos 9 secs not but pct 90% requires 6s -> true via pct
    REQUIRE(shouldMarkPlayedEx(10.0, 6.5, false, 60, 90) == true);
    // pos less than both thresholds -> false
    REQUIRE(shouldMarkPlayedEx(200.0, 10.0, false, 60, 90) == false);
}

TEST_CASE("shouldMarkPlayed default 60 90 wrapper", "[engine_history]") {
    using caudio::engine::detail::shouldMarkPlayed;
    REQUIRE(shouldMarkPlayed(100.0, 61.0, false) == true);
    REQUIRE(shouldMarkPlayed(100.0, 30.0, false) == false);
    REQUIRE(shouldMarkPlayed(100.0, 80.0, true) == false);
}

TEST_CASE("CAS exactly-once markedPlayed atomic", "[engine_history]") {
    std::atomic<bool> marked{false};
    bool e = false;
    REQUIRE(marked.compare_exchange_strong(e, true));
    // second CAS with expected false should fail because already true
    e = false;
    REQUIRE(!marked.compare_exchange_strong(e, true));
    REQUIRE(marked.load());
    // reset and succeed again
    marked.store(false);
    e = false;
    REQUIRE(marked.compare_exchange_strong(e, true));
}

TEST_CASE("history insert increments play_count and last_played", "[engine_history]") {
    std::string dbPath = tempDbPath("eng_hist_inc").string();
    auto dbRes = Database::open(dbPath);
    REQUIRE(dbRes.has_value());
    auto db = std::move(dbRes.value());
    Track t;
    t.path = "hist.wav";
    t.duration = 10.0;
    for (int b = 0; b < 32; ++b)
        t.fingerprint[b] = (uint8_t)(0x60 + b);
    auto ins = db->insertTrack(t);
    REQUIRE(ins.has_value());
    int64_t tid = *ins;
    auto before = db->getTrack(tid);
    REQUIRE(before.has_value());
    REQUIRE(before->play_count == 0);
    // ensure queue has track for engine play
    REQUIRE(db->queueEnqueue(1, tid, -1).has_value());
    // engine with small thresholds: 10% and 1 sec, poll 10ms
    EngineConfig cfg;
    cfg.enableMonitorThread = true;
    cfg.pollMs = 10;
    cfg.historyThresholdPct = 10;
    cfg.historyThresholdSecs = 1;
    cfg.gaplessMs = 300;
    auto eRes = Engine::create(cfg);
    REQUIRE(eRes.has_value());
    auto eng = std::move(eRes.value());
    REQUIRE(eng->attachDb(std::move(db)).has_value());
    REQUIRE(eng->play(1).has_value());
    // wait deterministically for history to be marked (threshold 1s or 10% of 10s=1s, so ~1s)
    // poll every 50ms with overall timeout 2000ms, breaking early when condition met
    auto checkMarked = [&]() -> bool {
        sqlite3* h = nullptr;
        if (sqlite3_open_v2(dbPath.c_str(), &h, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
            return false;
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(h, "SELECT play_count,last_played FROM tracks WHERE id=?", -1, &st,
                               nullptr) != SQLITE_OK) {
            sqlite3_close(h);
            return false;
        }
        sqlite3_bind_int64(st, 1, tid);
        bool ok = false;
        if (sqlite3_step(st) == SQLITE_ROW) {
            int64_t pc = sqlite3_column_int64(st, 0);
            int64_t lp = sqlite3_column_int64(st, 1);
            if (pc >= 1 && lp > 0)
                ok = true;
        }
        sqlite3_finalize(st);
        sqlite3_close(h);
        return ok;
    };
    busyWaitUntil(checkMarked, std::chrono::milliseconds(2000), std::chrono::milliseconds(50));
    REQUIRE(checkMarked());
    // also check exactly-once: after marking, play_count should stay 1 not 2 (monitor interval
    // 10ms) poll stability: wait 300ms then verify still 1
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    sqlite3* h2 = nullptr;
    REQUIRE(sqlite3_open_v2(dbPath.c_str(), &h2, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* st2 = nullptr;
    REQUIRE(sqlite3_prepare_v2(h2, "SELECT play_count FROM tracks WHERE id=?", -1, &st2, nullptr) ==
            SQLITE_OK);
    sqlite3_bind_int64(st2, 1, tid);
    REQUIRE(sqlite3_step(st2) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int64(st2, 0) == 1);
    sqlite3_finalize(st2);
    sqlite3_close(h2);
    // also history row exists
    sqlite3* h3 = nullptr;
    REQUIRE(sqlite3_open_v2(dbPath.c_str(), &h3, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* st3 = nullptr;
    REQUIRE(sqlite3_prepare_v2(h3, "SELECT COUNT(*) FROM history WHERE track_id=?", -1, &st3,
                               nullptr) == SQLITE_OK);
    sqlite3_bind_int64(st3, 1, tid);
    REQUIRE(sqlite3_step(st3) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int64(st3, 0) == 1);
    sqlite3_finalize(st3);
    sqlite3_close(h3);
    eng.reset();
    {
        std::error_code ec;
        std::filesystem::remove(dbPath, ec);
        std::filesystem::remove(dbPath + "-wal", ec);
        std::filesystem::remove(dbPath + "-shm", ec);
    }
}






