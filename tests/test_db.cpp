#include <catch2/catch_test_macros.hpp>
#include <array>
#include <filesystem>
#include <string>

import caudio.db;
import caudio.utils;

using namespace caudio::db;
using namespace caudio::utils;

static Track makeTrack(int idx, const std::string& pathOverride = "") {
    Track t;
    t.path = pathOverride.empty() ? ("/tmp/track_" + std::to_string(idx) + ".mp3") : pathOverride;
    t.title = "Title" + std::to_string(idx);
    t.artist = "Artist" + std::to_string(idx);
    t.album = "Album" + std::to_string(idx);
    for (int b = 0; b < 32; ++b) t.fingerprint[b] = (uint8_t)(idx * 37 + b * 7);
    t.duration = 1.0 + idx;
    t.library_id = 1;
    return t;
}

TEST_CASE("insertTrack duplicate AlreadyExists", "[db]") {
    auto dbRes = Database::open(":memory:");
    REQUIRE(dbRes.has_value());
    auto db = std::move(dbRes.value());
    Track t = makeTrack(1);
    auto r1 = db->insertTrack(t);
    REQUIRE(r1.has_value());
    // fingerprint is UNIQUE per schema; duplicate fingerprint should be AlreadyExists
    Track t2 = makeTrack(1);
    // ensure same fingerprint but different path to prove fingerprint uniqueness
    t2.path = "/tmp/track_dup_path.mp3";
    auto r2 = db->insertTrack(t2);
    REQUIRE(!r2.has_value());
    REQUIRE(r2.error().code == StatusCode::AlreadyExists);
    // same path with different fingerprint should succeed (path not unique)
    Track t3 = makeTrack(99, t.path);
    for (int b=0;b<32;++b) t3.fingerprint[b] = (uint8_t)(0xFF - b);
    auto r3 = db->insertTrack(t3);
    // this should succeed because fingerprint differs and path allows duplicates
    REQUIRE(r3.has_value());
}

TEST_CASE("updateTrack missing NotFound", "[db]") {
    auto dbRes = Database::open(":memory:");
    REQUIRE(dbRes.has_value());
    auto db = std::move(dbRes.value());
    Track t = makeTrack(2);
    t.id = 9999; // non-existent id
    auto r = db->updateTrack(t);
    REQUIRE(!r.has_value());
    REQUIRE(r.error().code == StatusCode::NotFound);
    // updating id 0 should be InvalidArg
    t.id = 0;
    auto r2 = db->updateTrack(t);
    REQUIRE(!r2.has_value());
    REQUIRE(r2.error().code == StatusCode::InvalidArg);
}

TEST_CASE("getTrack NotFound", "[db]") {
    auto dbRes = Database::open(":memory:");
    REQUIRE(dbRes.has_value());
    auto db = std::move(dbRes.value());
    auto r = db->getTrack(9999);
    REQUIRE(!r.has_value());
    REQUIRE(r.error().code == StatusCode::NotFound);
    auto r2 = db->getTrack(0);
    REQUIRE(!r2.has_value());
    REQUIRE(r2.error().code == StatusCode::InvalidArg);
}

TEST_CASE("deleteTrack and verify", "[db]") {
    auto dbRes = Database::open(":memory:");
    REQUIRE(dbRes.has_value());
    auto db = std::move(dbRes.value());
    Track t = makeTrack(3);
    auto ins = db->insertTrack(t);
    REQUIRE(ins.has_value());
    int64_t id = *ins;
    REQUIRE(db->deleteTrack(id).has_value());
    auto g = db->getTrack(id);
    REQUIRE(!g.has_value());
    REQUIRE(g.error().code == StatusCode::NotFound);
    // delete again -> NotFound
    auto del2 = db->deleteTrack(id);
    REQUIRE(!del2.has_value());
    REQUIRE(del2.error().code == StatusCode::NotFound);
    // delete 0 -> InvalidArg
    REQUIRE(!db->deleteTrack(0).has_value());
    REQUIRE(db->deleteTrack(0).error().code == StatusCode::InvalidArg);
}

TEST_CASE("listTracks pagination limit/offset", "[db]") {
    auto dbRes = Database::open(":memory:");
    REQUIRE(dbRes.has_value());
    auto db = std::move(dbRes.value());
    for (int i=0;i<5;++i) {
        Track t = makeTrack(10+i);
        auto r = db->insertTrack(t);
        REQUIRE(r.has_value());
    }
    TrackQuery q;
    q.limit = 2;
    q.offset = 0;
    auto l1 = db->listTracks(&q);
    REQUIRE(l1.has_value());
    REQUIRE(l1->size() == 2);
    q.offset = 2;
    auto l2 = db->listTracks(&q);
    REQUIRE(l2.has_value());
    REQUIRE(l2->size() == 2);
    REQUIRE((*l2)[0].id != (*l1)[0].id);
    q.offset = 4;
    q.limit = 2;
    auto l3 = db->listTracks(&q);
    REQUIRE(l3.has_value());
    REQUIRE(l3->size() == 1);
    q.offset = 10;
    auto l4 = db->listTracks(&q);
    REQUIRE(l4.has_value());
    REQUIRE(l4->empty());
    // limit 0 means no limit (all)
    q.limit = 0;
    q.offset = 0;
    auto l5 = db->listTracks(&q);
    REQUIRE(l5.has_value());
    REQUIRE(l5->size() == 5);
    // offset only (no limit)
    q.limit = 0;
    q.offset = 3;
    auto l6 = db->listTracks(&q);
    REQUIRE(l6.has_value());
    REQUIRE(l6->size() == 2);
}

TEST_CASE("findByFingerprint and findByPath", "[db]") {
    auto dbRes = Database::open(":memory:");
    REQUIRE(dbRes.has_value());
    auto db = std::move(dbRes.value());
    Track t = makeTrack(42, "/tmp/find_test.mp3");
    auto ins = db->insertTrack(t);
    REQUIRE(ins.has_value());
    // findByPath success
    auto byPath = db->findByPath(t.path);
    REQUIRE(byPath.has_value());
    REQUIRE(byPath->id == *ins);
    // findByPath missing
    auto byPathMiss = db->findByPath("/nonexistent/path.mp3");
    REQUIRE(!byPathMiss.has_value());
    REQUIRE(byPathMiss.error().code == StatusCode::NotFound);
    // findByFingerprint success
    auto byFp = db->findByFingerprint(t.fingerprint);
    REQUIRE(byFp.has_value());
    REQUIRE(byFp->id == *ins);
    // findByFingerprint missing
    std::array<uint8_t,32> wrong{};
    wrong.fill(0xFF);
    auto byFpMiss = db->findByFingerprint(wrong);
    REQUIRE(!byFpMiss.has_value());
    REQUIRE(byFpMiss.error().code == StatusCode::NotFound);
}

// Keep original sqlite sanity tests but adapted to use Database::open path check
TEST_CASE("SQLite open in-memory via Database", "[db_sqlite]") {
    auto dbRes = Database::open(":memory:");
    REQUIRE(dbRes.has_value());
    REQUIRE(dbRes.value()->handle() != nullptr);
}






