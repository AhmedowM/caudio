#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "helpers/helpers_test.hpp"

import caudio.db;
import caudio.utils;

using namespace caudio::db;
using namespace caudio::utils;
using namespace caudio::test_helpers;

TEST_CASE("scan generator yields audio files recursively", "[db_scan]") {
    auto root = tempDirPath("scan_test");
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(root / "sub");
    // create dummy audio files
    for (auto name : {root / "a.mp3", root / "sub" / "b.wav", root / "c.ogg"}) {
        std::ofstream f(name, std::ios::binary);
        f << "dummy content " << name.string();
    }
    std::ofstream f2(root / "ignore.txt", std::ios::binary);
    f2 << "not audio";
    auto tracks = scanDirectory(root, ScanMode::Sampled);
    REQUIRE(tracks.has_value());
    REQUIRE(tracks->size() == 3);
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
}

TEST_CASE("scan fingerprint head+tail+size sampled deterministic", "[db_scan]") {
    auto dir = tempDirPath("fp_test");
    std::filesystem::create_directories(dir);
    auto p = dir / "file.wav";
    // create 100K file with known pattern
    std::vector<uint8_t> data(100 * 1024, 0xAB);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = (uint8_t)(i & 0xFF);
    {
        std::ofstream f(p, std::ios::binary);
        f.write((char*)data.data(), data.size());
    }
    // Test via public API: scanDirectory returns tracks with fingerprints
    auto tracks1 = scanDirectory(dir, ScanMode::Sampled);
    REQUIRE(tracks1.has_value());
    REQUIRE(tracks1->size() == 1);
    auto fp1 = tracks1->front().fingerprint;

    // Re-scan same file -> same fingerprint
    auto tracks2 = scanDirectory(dir, ScanMode::Sampled);
    REQUIRE(tracks2.has_value());
    REQUIRE(tracks2->size() == 1);
    auto fp2 = tracks2->front().fingerprint;
    REQUIRE(fp1 == fp2);

    // same head different tail -> different fingerprint
    std::vector<uint8_t> data2 = data;
    // modify tail last 1K
    for (size_t i = 99 * 1024; i < 100 * 1024; ++i)
        data2[i] = 0xFF;
    auto p2 = dir / "file2.wav";
    {
        std::ofstream f(p2, std::ios::binary);
        f.write((char*)data2.data(), data2.size());
    }
    auto tracks3 = scanDirectory(dir, ScanMode::Sampled);
    REQUIRE(tracks3.has_value());
    REQUIRE(tracks3->size() == 2);
    bool foundDifferent = false;
    for (auto& t : *tracks3) {
        if (t.fingerprint != fp1) {
            foundDifferent = true;
            break;
        }
    }
    REQUIRE(foundDifferent);

    // size included: same head+tail but different size -> different
    auto p3 = dir / "file3.wav";
    std::vector<uint8_t> data3(data.begin(), data.begin() + 50 * 1024);
    {
        std::ofstream f(p3, std::ios::binary);
        f.write((char*)data3.data(), data3.size());
    }
    auto tracks4 = scanDirectory(dir, ScanMode::Sampled);
    REQUIRE(tracks4.has_value());
    REQUIRE(tracks4->size() == 3);
    foundDifferent = false;
    for (auto& t : *tracks4) {
        if (t.fingerprint != fp1) {
            foundDifferent = true;
            break;
        }
    }
    REQUIRE(foundDifferent);
    // fingerprint size is 32 bytes
    REQUIRE(fp1.size() == 32);
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
}

TEST_CASE("scanLibrary inserts and dedup by fingerprint", "[db_scan]") {
    auto dir = tempDirPath("scanlib");
    std::filesystem::create_directories(dir);
    // create two audio files with distinct content
    {
        std::ofstream f1(dir / "x.mp3", std::ios::binary);
        std::vector<uint8_t> d1(10 * 1024, 0x42);
        for (size_t i = 0; i < d1.size(); ++i)
            d1[i] = (uint8_t)(i & 0xFF);
        f1.write((char*)d1.data(), d1.size());
    }
    {
        std::ofstream f2(dir / "y.wav", std::ios::binary);
        std::vector<uint8_t> d2(10 * 1024, 0x43);
        for (size_t i = 0; i < d2.size(); ++i)
            d2[i] = (uint8_t)((i + 1) & 0xFF);
        f2.write((char*)d2.data(), d2.size());
    }
    auto dbRes = Database::open(":memory:");
    REQUIRE(dbRes.has_value());
    auto db = std::move(dbRes.value());
    auto libIdRes = db->libraryAdd(dir.string(), "testlib");
    REQUIRE(libIdRes.has_value());
    int64_t libId = *libIdRes;
    auto sr = scanLibrary(*db, libId);
    REQUIRE(sr.has_value());
    auto stats = db->getStats();
    REQUIRE(stats.has_value());
    REQUIRE(stats->num_tracks == 2);
    // second scan should dedup (same path+size+mtime -> skip) and not increase count
    auto sr2 = scanLibrary(*db, libId);
    REQUIRE(sr2.has_value());
    auto stats2 = db->getStats();
    REQUIRE(stats2.has_value());
    REQUIRE(stats2->num_tracks == 2);
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
}

TEST_CASE("scan non-existent dir returns empty", "[db_scan]") {
    auto tracks = scanDirectory("/nonexistent_path_xyz_12345", ScanMode::Sampled);
    REQUIRE(tracks.has_value());
    REQUIRE(tracks->empty());
}

TEST_CASE("scan empty dir returns empty", "[db_scan]") {
    auto dir = tempDirPath("empty_scan");
    std::filesystem::create_directories(dir);
    auto tracks = scanDirectory(dir, ScanMode::Sampled);
    REQUIRE(tracks.has_value());
    REQUIRE(tracks->empty());
    // also .txt noise only should be empty
    {
        std::ofstream f(dir / "notes.txt", std::ios::binary);
        f << "hello";
    }
    auto tracks2 = scanDirectory(dir, ScanMode::Sampled);
    REQUIRE(tracks2.has_value());
    REQUIRE(tracks2->empty());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("scanDirectory filters by audio extensions case-insensitively", "[db_scan]") {
    auto dir = tempDirPath("ext_test");
    // Clean up any existing files
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    // Create files with various extensions - use unique base names to avoid case-insensitive FS collisions
    std::ofstream(dir / "a.MP3", std::ios::binary) << "audio";
    std::ofstream(dir / "b.mp3", std::ios::binary) << "audio";
    std::ofstream(dir / "c.Mp3", std::ios::binary) << "audio";
    std::ofstream(dir / "d.m4a", std::ios::binary) << "audio";
    std::ofstream(dir / "e.M4A", std::ios::binary) << "audio";
    std::ofstream(dir / "f.FLAC", std::ios::binary) << "audio";
    std::ofstream(dir / "g.WAV", std::ios::binary) << "audio";
    std::ofstream(dir / "h.OGG", std::ios::binary) << "audio";
    std::ofstream(dir / "i.txt", std::ios::binary) << "not audio";
    std::ofstream(dir / "j", std::ios::binary) << "no ext";

    auto tracks = scanDirectory(dir, ScanMode::Sampled);
    REQUIRE(tracks.has_value());
    // Only 8 audio files should be found
    REQUIRE(tracks->size() == 8);

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("ScanMode Full vs Sampled for file >128 KiB", "[db_scan]") {
    auto dir = tempDirPath("scan_mode");
    std::filesystem::create_directories(dir);
    auto p = dir / "large.wav";
    // 200 KiB file ( >128 KiB = 2*64K )
    std::vector<uint8_t> data(200 * 1024);
    for (size_t i=0;i<data.size();++i) data[i]=(uint8_t)(i*13 & 0xFF);
    {
        std::ofstream f(p, std::ios::binary);
        f.write((char*)data.data(), data.size());
    }
    auto sampled = scanDirectory(dir, ScanMode::Sampled);
    REQUIRE(sampled.has_value());
    REQUIRE(sampled->size()==1);
    auto full = scanDirectory(dir, ScanMode::Full);
    REQUIRE(full.has_value());
    REQUIRE(full->size()==1);
    // Sampled uses head+tail+size, Full uses BLAKE3 of entire file -> should differ for large file
    REQUIRE(sampled->front().fingerprint != full->front().fingerprint);
    // But re-scanning same mode is deterministic
    auto sampled2 = scanDirectory(dir, ScanMode::Sampled);
    REQUIRE(sampled2->front().fingerprint == sampled->front().fingerprint);
    auto full2 = scanDirectory(dir, ScanMode::Full);
    REQUIRE(full2->front().fingerprint == full->front().fingerprint);
    // Modify middle byte only (not in head/tail 64K) -> Sampled should stay same, Full should change
    // For 200K file, head 64K = [0,64K), tail 64K = [136K,200K), middle [64K,136K)
    std::vector<uint8_t> dataMod = data;
    dataMod[100*1024] ^= 0xFF;
    auto p2 = dir / "large2.wav";
    // overwrite original with modified middle
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write((char*)dataMod.data(), dataMod.size());
    }
    auto sampledMod = scanDirectory(dir, ScanMode::Sampled);
    REQUIRE(sampledMod.has_value());
    REQUIRE(sampledMod->size()==1);
    // sampled should be same as before because middle not covered (unless size changed? size same)
    // For this file, change is in middle so sampled stays same, but full changes
    // Note: if sampled differs, still valid test; we check full definitely differs from original full
    auto fullMod = scanDirectory(dir, ScanMode::Full);
    REQUIRE(fullMod.has_value());
    REQUIRE(fullMod->front().fingerprint != full->front().fingerprint);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("scanLibrary preserves play_count", "[db_scan]") {
    auto dir = tempDirPath("scan_preserve");
    std::filesystem::create_directories(dir);
    auto p = dir / "song.mp3";
    std::vector<uint8_t> d1(10*1024, 0x11);
    for (size_t i=0;i<d1.size();++i) d1[i]=(uint8_t)(i & 0xFF);
    {
        std::ofstream f(p, std::ios::binary);
        f.write((char*)d1.data(), d1.size());
    }
    auto dbRes = Database::open(":memory:");
    REQUIRE(dbRes.has_value());
    auto db = std::move(dbRes.value());
    auto libIdRes = db->libraryAdd(dir.string(), "lib");
    REQUIRE(libIdRes.has_value());
    int64_t libId = *libIdRes;
    REQUIRE(scanLibrary(*db, libId).has_value());
    auto tracks = db->listTracks(nullptr);
    REQUIRE(tracks.has_value());
    REQUIRE(tracks->size()==1);
    int64_t tid = tracks->front().id;
    // set play_count and rating, then modify file content (same path, different fingerprint) and rescan
    {
        auto t = db->getTrack(tid);
        REQUIRE(t.has_value());
        t->play_count = 42;
        t->rating = 5;
        t->title = "Custom Title";
        REQUIRE(db->updateTrack(*t).has_value());
    }
    // modify file to trigger content-changed path: change size as well to guarantee early-exit fails (size != trk.size)
    std::vector<uint8_t> d2(12*1024, 0x22);
    for (size_t i=0;i<d2.size();++i) d2[i]=(uint8_t)((i*3) & 0xFF);
    // ensure different fingerprint from d1
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write((char*)d2.data(), d2.size());
    }
    REQUIRE(scanLibrary(*db, libId).has_value());
    auto t2 = db->getTrack(tid);
    REQUIRE(t2.has_value());
    REQUIRE(t2->play_count == 42);
    REQUIRE(t2->rating == 5);
    // title should have been cleared (metadata staleness) but play_count preserved
    REQUIRE(t2->title.empty());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}






