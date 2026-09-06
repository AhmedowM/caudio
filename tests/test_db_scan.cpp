#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
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
    for(auto name: {root / "a.mp3", root / "sub" / "b.wav", root / "c.ogg"}){
        std::ofstream f(name, std::ios::binary);
        f << "dummy content " << name.string();
    }
    std::ofstream f2(root / "ignore.txt", std::ios::binary); f2 << "not audio";
    auto tracks = scanDirectory(root, ScanMode::Sampled);
    REQUIRE(tracks.has_value());
    REQUIRE(tracks->size()==3);
    { std::error_code ec; std::filesystem::remove_all(root,ec); }
}

TEST_CASE("computeFingerprint head+tail+size sampled deterministic", "[db_scan]") {
    auto dir = tempDirPath("fp_test");
    std::filesystem::create_directories(dir);
    auto p = dir / "file.wav";
    // create 100K file with known pattern
    std::vector<uint8_t> data(100*1024, 0xAB);
    for(size_t i=0;i<data.size();++i) data[i]=(uint8_t)(i & 0xFF);
    { std::ofstream f(p, std::ios::binary); f.write((char*)data.data(), data.size());}
    auto fp1 = computeFingerprint(p);
    REQUIRE(fp1.has_value());
    auto fp2 = computeFingerprint(p);
    REQUIRE(fp2.has_value());
    REQUIRE(*fp1 == *fp2);
    // same head different tail -> different fingerprint
    std::vector<uint8_t> data2 = data;
    // modify tail last 1K
    for(size_t i=99*1024;i<100*1024;++i) data2[i]=0xFF;
    auto p2 = dir / "file2.wav";
    { std::ofstream f(p2, std::ios::binary); f.write((char*)data2.data(), data2.size());}
    auto fp3 = computeFingerprint(p2);
    REQUIRE(fp3.has_value());
    REQUIRE(*fp1 != *fp3);
    // size included: same head+tail but different size -> different
    auto p3 = dir / "file3.wav";
    std::vector<uint8_t> data3(data.begin(), data.begin()+50*1024);
    { std::ofstream f(p3, std::ios::binary); f.write((char*)data3.data(), data3.size());}
    auto fp4 = computeFingerprint(p3);
    REQUIRE(fp4.has_value());
    REQUIRE(*fp1 != *fp4);
    REQUIRE(fp1->size()==32);
    { std::error_code ec; std::filesystem::remove_all(dir,ec); }
}

TEST_CASE("scanLibrary inserts and dedup by fingerprint", "[db_scan]") {
    auto dir = tempDirPath("scanlib");
    std::filesystem::create_directories(dir);
    // create two audio files with distinct content
    {
        std::ofstream f1(dir / "x.mp3", std::ios::binary);
        std::vector<uint8_t> d1(10*1024, 0x42);
        for(size_t i=0;i<d1.size();++i) d1[i]=(uint8_t)(i&0xFF);
        f1.write((char*)d1.data(), d1.size());
    }
    {
        std::ofstream f2(dir / "y.wav", std::ios::binary);
        std::vector<uint8_t> d2(10*1024, 0x43);
        for(size_t i=0;i<d2.size();++i) d2[i]=(uint8_t)((i+1)&0xFF);
        f2.write((char*)d2.data(), d2.size());
    }
    auto dbRes = Database::open(":memory:"); REQUIRE(dbRes.has_value());
    auto db = std::move(dbRes.value());
    auto libIdRes = db->libraryAdd(dir.string(), "testlib");
    REQUIRE(libIdRes.has_value());
    int64_t libId = *libIdRes;
    auto sr = scanLibrary(*db, libId);
    REQUIRE(sr.has_value());
    auto stats = db->getStats(); REQUIRE(stats.has_value());
    REQUIRE(stats->num_tracks == 2);
    // second scan should dedup (same path+size+mtime -> skip) and not increase count
    auto sr2 = scanLibrary(*db, libId);
    REQUIRE(sr2.has_value());
    auto stats2 = db->getStats(); REQUIRE(stats2.has_value());
    REQUIRE(stats2->num_tracks == 2);
    { std::error_code ec; std::filesystem::remove_all(dir,ec); }
}

TEST_CASE("scan non-existent dir returns empty", "[db_scan]") {
    auto tracks = scanDirectory("/nonexistent_path_xyz_12345", ScanMode::Sampled);
    REQUIRE(tracks.has_value());
    REQUIRE(tracks->empty());
}
