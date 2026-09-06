#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <thread>
#include <chrono>
#include <atomic>

import caudio.db;
import caudio.engine;
import caudio.utils;

using namespace caudio::db;
using namespace caudio::engine;
using namespace caudio::utils;

static std::string gpath(const std::string& n){
    return (std::filesystem::temp_directory_path() / (n + "_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()) + ".db")).string();
}

TEST_CASE("gapless CAS arms once", "[engine_gapless]") {
    std::atomic<bool> armed{false};
    bool expected=false;
    REQUIRE(armed.compare_exchange_strong(expected,true,std::memory_order_acq_rel,std::memory_order_acquire));
    expected=false;
    REQUIRE(!armed.compare_exchange_strong(expected,true,std::memory_order_acq_rel,std::memory_order_acquire));
    armed.store(false,std::memory_order_release);
    expected=false;
    REQUIRE(armed.compare_exchange_strong(expected,true,std::memory_order_acq_rel,std::memory_order_acquire));
}

TEST_CASE("gapless 300ms lookahead triggers next once", "[engine_gapless]") {
    std::string dbPath=gpath("gapless");
    auto dbRes=Database::open(dbPath); REQUIRE(dbRes.has_value());
    auto db=std::move(dbRes.value());
    for(int i=0;i<2;++i){Track t; t.path="gap"+std::to_string(i)+".wav"; t.duration=0.8; for(int b=0;b<32;++b) t.fingerprint[b]=(uint8_t)(0xC0+i*16+b); auto r=db->insertTrack(t); REQUIRE(r.has_value()); REQUIRE(db->queueEnqueue(1,*r,-1).has_value());}
    EngineConfig cfg; cfg.enableMonitorThread=true; cfg.pollMs=10; cfg.gaplessMs=300; cfg.historyThresholdPct=100; cfg.historyThresholdSecs=100;
    auto eRes=Engine::create(cfg); REQUIRE(eRes.has_value());
    auto eng=std::move(eRes.value());
    REQUIRE(eng->attachDb(std::move(db)).has_value());
    REQUIRE(eng->play(1).has_value());
    int64_t first = eng->currentTrackId();
    // duration 0.8, gap 0.3 -> when remaining <=0.3 (pos>=0.5) should arm and trigger next
    // wait 600ms -> pos ~0.6 should have triggered
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    int64_t cur = eng->currentTrackId();
    // should have advanced to second track via gapless
    REQUIRE(cur != first);
    // ensure only once: wait another 300ms, should not advance again (queue now empty or last track)
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    int64_t cur2 = eng->currentTrackId();
    REQUIRE(cur2 == cur);
    eng.reset();
    { std::error_code ec; std::filesystem::remove(dbPath,ec); std::filesystem::remove(dbPath+"-wal",ec); std::filesystem::remove(dbPath+"-shm",ec); }
}

TEST_CASE("gapless not triggered when remaining > gap", "[engine_gapless]") {
    std::string dbPath=gpath("gapless_notrig");
    auto dbRes=Database::open(dbPath); REQUIRE(dbRes.has_value());
    auto db=std::move(dbRes.value());
    for(int i=0;i<2;++i){Track t; t.path="gap_n"+std::to_string(i)+".wav"; t.duration=5.0; for(int b=0;b<32;++b) t.fingerprint[b]=(uint8_t)(0xD0+i*16+b); auto r=db->insertTrack(t); REQUIRE(r.has_value()); REQUIRE(db->queueEnqueue(1,*r,-1).has_value());}
    EngineConfig cfg; cfg.enableMonitorThread=true; cfg.pollMs=10; cfg.gaplessMs=300;
    auto eRes=Engine::create(cfg); REQUIRE(eRes.has_value());
    auto eng=std::move(eRes.value());
    REQUIRE(eng->attachDb(std::move(db)).has_value());
    REQUIRE(eng->play(1).has_value());
    int64_t first=eng->currentTrackId();
    // duration 5s, gap 0.3 -> remaining 4.8 > gap, should not trigger within 200ms
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    REQUIRE(eng->currentTrackId()==first);
    // seek near end then gapless should trigger
    REQUIRE(eng->seek(4.8).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    // after seek remaining 0.2 <= gap, should have advanced
    // poll until change or timeout 1s
    auto start=std::chrono::steady_clock::now();
    while(eng->currentTrackId()==first && std::chrono::steady_clock::now()-start < std::chrono::milliseconds(800)){
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(eng->currentTrackId()!=first);
    eng.reset();
    { std::error_code ec; std::filesystem::remove(dbPath,ec); std::filesystem::remove(dbPath+"-wal",ec); std::filesystem::remove(dbPath+"-shm",ec); }
}
