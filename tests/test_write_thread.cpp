#include <sqlite3.h>

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <string>
#include <string_view>
#include <thread>
import caudio.utils;
import caudio.db;

using namespace caudio::utils;

TEST_CASE("WriterThread bounded full BUSY and flush timeout 200ms", "[db][writer_thread]") {
    // Use MpscQueue directly to verify Bounded full -> Busy
    caudio::utils::MpscQueue<int> q(2);
    REQUIRE(q.push(1).has_value());
    REQUIRE(q.push(2).has_value());
    auto r = q.push(3);
    REQUIRE(!r.has_value());
    REQUIRE(r.error().code == caudio::utils::StatusCode::Busy);

    // Real WriterThread: capacity 2, do NOT open so queue never drains -> flush should timeout after ~200ms with Busy
    {
        caudio::db::WriterThread wt(2);
        // push two ops without opening thread -> queue stays full
        // need to construct WriteOp-like pushes via WriterThread::push
        // Use sql strings; stmt nullptr
        auto pr1 = wt.push("SELECT 1;", nullptr, nullptr);
        REQUIRE(pr1.has_value());
        auto pr2 = wt.push("SELECT 1;", nullptr, nullptr);
        REQUIRE(pr2.has_value());
        auto pr3 = wt.push("SELECT 1;", nullptr, nullptr);
        REQUIRE(!pr3.has_value());
        REQUIRE(pr3.error().code == caudio::utils::StatusCode::Busy);
        auto start = std::chrono::steady_clock::now();
        auto fr = wt.flush();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        REQUIRE(!fr.has_value());
        REQUIRE(fr.error().code == caudio::utils::StatusCode::Busy);
        REQUIRE(elapsed.count() >= 190);
        REQUIRE(elapsed.count() < 600);
        // after draining by close, flush should succeed quickly
        wt.close();
        // after close queue drained
        auto fr2 = wt.flush();
        REQUIRE(fr2.has_value());
    }
    // Flush succeeds when queue empty
    {
        caudio::db::WriterThread wt2(4);
        auto start = std::chrono::steady_clock::now();
        auto fr = wt2.flush();
        REQUIRE(fr.has_value());
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        REQUIRE(elapsed.count() < 50);
    }
    // After open, flush drains within 200ms (worker processes)
    {
        sqlite3* db=nullptr;
        REQUIRE(sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE, nullptr)==SQLITE_OK);
        REQUIRE(sqlite3_exec(db,"CREATE TABLE t(id INTEGER)",nullptr,nullptr,nullptr)==SQLITE_OK);
        caudio::db::WriterThread wt(4);
        wt.open(db);
        // push an insert using SQL string (stmt=nullptr)
        std::atomic<bool> cbCalled{false};
        auto pr = wt.push("INSERT INTO t(id) VALUES (1)", nullptr, [&](std::expected<void, caudio::utils::Error> e){ cbCalled.store(true); (void)e; });
        REQUIRE(pr.has_value());
        auto fr = wt.flush();
        REQUIRE(fr.has_value());
        // wait for callback outside lock (worker calls cb)
        auto start = std::chrono::steady_clock::now();
        while(!cbCalled.load() && std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500)) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        REQUIRE(cbCalled.load());
        wt.close();
        sqlite3_close(db);
    }
}





