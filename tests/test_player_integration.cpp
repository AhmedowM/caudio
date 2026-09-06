#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

#include "helpers/helpers_test.hpp"

import caudio.player;
import caudio.db;
import caudio.engine;
import caudio.utils;

using namespace caudio::player;
using namespace caudio::utils;
using namespace caudio::db;
using namespace caudio::engine;
using namespace caudio::test_helpers;

static std::string fixturePath(const std::string& name) {
    for (auto c : {std::string("tests/fixtures/") + name, std::string("../tests/fixtures/") + name,
                   std::string("C:/Users/Secondary/Projects/caudio-cpp/tests/fixtures/") + name}) {
        if (std::filesystem::exists(c))
            return c;
    }
#ifdef TEST_DATA_DIR
    std::string td = std::string(TEST_DATA_DIR) + "/" + name;
    if (std::filesystem::exists(td))
        return td;
#endif
    return "tests/fixtures/" + name;
}
TEST_CASE("player create open decode wav", "[player_integration]") {
    auto r = FileReader::open(fixturePath("sample.wav"));
    REQUIRE(r.has_value());
    auto dec = DecoderRegistry::open(**r);
    REQUIRE(dec.has_value());
    REQUIRE((*dec)->sampleRate() > 0);
    REQUIRE((*dec)->channels() > 0);
    std::array<float, 1024> out{};
    size_t got = (*dec)->decode(out);
    REQUIRE(got > 0);
}

TEST_CASE("player open invalid path returns NotFound", "[player_integration]") {
    auto r = FileReader::open("/nonexistent_xyz/sample.wav");
    REQUIRE(!r.has_value());
    REQUIRE(r.error().code == Result::NotFound);
}

TEST_CASE("player seek and volume", "[player_integration]") {
    auto r = FileReader::open(fixturePath("sample.wav"));
    REQUIRE(r.has_value());
    auto dec = DecoderRegistry::open(**r);
    REQUIRE(dec.has_value());
    REQUIRE((*dec)->seek(0.0).has_value());
    REQUIRE((*dec)->seek(-1.0).error().code == Result::InvalidArg);
    // audio output volume
    SpscRing<float> ring{8192, (*dec)->channels()};
    AudioOutput::Config cfg;
    cfg.sampleRate = (*dec)->sampleRate();
    cfg.channels = (*dec)->channels();
    cfg.ring = &ring;
    auto outRes = AudioOutput::create(cfg);
    REQUIRE(outRes.has_value());
    (*outRes)->setVolume(0.5f);
    REQUIRE((*outRes)->volume() == 0.5f);
    (*outRes)->setVolume(2.0f);
    REQUIRE((*outRes)->volume() == 1.0f);
    (*outRes)->setVolume(-1.0f);
    REQUIRE((*outRes)->volume() == 0.0f);
}

TEST_CASE("player position time-based via Engine steady_clock", "[player_integration]") {
    std::string dbPath = tempDbPath("pl_pos").string();
    {
        auto dbRes = Database::open(dbPath);
        REQUIRE(dbRes.has_value());
        auto db = std::move(dbRes.value());
        Track t;
        t.path = fixturePath("sample.wav");
        t.duration = 2.0;
        for (int b = 0; b < 32; ++b)
            t.fingerprint[b] = (uint8_t)(0x55 + b);
        auto ins = db->insertTrack(t);
        REQUIRE(ins.has_value());
        REQUIRE(db->queueEnqueue(1, *ins, -1).has_value());
        EngineConfig cfg;
        cfg.enableMonitorThread = false;
        auto eRes = Engine::create(cfg);
        REQUIRE(eRes.has_value());
        auto eng = std::move(eRes.value());
        REQUIRE(eng->attachDb(std::move(db)).has_value());
        REQUIRE(eng->play(1).has_value());
        REQUIRE(eng->state() == PlaybackState::Playing);
        double pos0 = eng->position();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        double pos1 = eng->position();
        REQUIRE(pos1 > pos0);
        REQUIRE(pos1 >= 0.08);
        // pause should freeze position
        REQUIRE(eng->pause().has_value());
        REQUIRE(eng->state() == PlaybackState::Paused);
        double paused = eng->position();
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        REQUIRE(eng->position() == paused);
        // resume should advance
        REQUIRE(eng->resume().has_value());
        REQUIRE(eng->state() == PlaybackState::Playing);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        REQUIRE(eng->position() > paused);
        REQUIRE(eng->stop().has_value());
        REQUIRE(eng->state() == PlaybackState::Stopped);
    }
    std::error_code ec;
    std::filesystem::remove(dbPath, ec);
    std::filesystem::remove(dbPath + "-wal", ec);
    std::filesystem::remove(dbPath + "-shm", ec);
}

TEST_CASE("player state transitions invalid", "[player_integration]") {
    EngineConfig cfg;
    cfg.enableMonitorThread = false;
    auto eRes = Engine::create(cfg);
    REQUIRE(eRes.has_value());
    auto eng = std::move(eRes.value());
    // without db, play should fail State
    REQUIRE(!eng->play(1).has_value());
    REQUIRE(eng->play(1).error().code == Result::State);
    // pause when stopped
    REQUIRE(!eng->pause().has_value());
    REQUIRE(eng->pause().error().code == Result::State);
    // resume when not paused
    REQUIRE(!eng->resume().has_value());
}

TEST_CASE("player decode ogg via ffmpeg", "[player_integration]") {
    auto r = FileReader::open(fixturePath("sample.ogg"));
    REQUIRE(r.has_value());
    auto dec = DecoderRegistry::open(**r);
    REQUIRE(dec.has_value());
    std::array<float, 512> out{};
    REQUIRE((*dec)->decode(out) > 0);
    REQUIRE((*dec)->seek(0.0).has_value());
    REQUIRE((*dec)->decode(out) > 0);
}
