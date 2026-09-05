#include <catch2/catch_test_macros.hpp>
#include <span>
#include <vector>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>

import caudio.player;
import caudio.utils;

using namespace caudio::player;
using namespace caudio::utils;

TEST_CASE("decode registry probe unsupported", "[decoder]") {
  std::vector<std::byte> sig(32, std::byte{'X'});
  auto r = MemoryReader::open(sig);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(!dec.has_value());
  REQUIRE(dec.error().code == Result::Unsupported);
}

TEST_CASE("decode registry probe restore offset with real ogg", "[decoder]") {
  auto path = std::filesystem::path(std::string(TEST_DATA_DIR) + "/sample.ogg");
  auto r = FileReader::open(path);
  REQUIRE(r.has_value());
  REQUIRE((**r).seek(10, SEEK_SET).has_value());
  REQUIRE((**r).tell() == 10);
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE((**r).tell() == 10);
}

TEST_CASE("ffmpeg decoder ogg real", "[decoder]") {
  auto path = std::filesystem::path(std::string(TEST_DATA_DIR) + "/sample.ogg");
  auto r = FileReader::open(path);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE((*dec)->sampleRate() > 0);
  REQUIRE((*dec)->channels() > 0);
  std::array<float, 1024> out{};
  std::size_t got = (*dec)->decode(out);
  REQUIRE(got > 0);
  bool nonZero = false;
  for (float f : out) if (std::abs(f) > 1e-6f) nonZero = true;
  REQUIRE(nonZero);
  REQUIRE((*dec)->seek(0.1).has_value());
}

TEST_CASE("ffmpeg decoder wav real", "[decoder]") {
  auto path = std::filesystem::path(std::string(TEST_DATA_DIR) + "/sample.wav");
  auto r = FileReader::open(path);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE((*dec)->sampleRate() > 0);
  REQUIRE((*dec)->channels() > 0);
  std::array<float, 1024> out{};
  std::size_t got = (*dec)->decode(out);
  REQUIRE(got > 0);
  bool nonZero = false;
  for (float f : out) if (std::abs(f) > 1e-6f) nonZero = true;
  REQUIRE(nonZero);
  REQUIRE((*dec)->seek(0.1).has_value());
}

TEST_CASE("decoder seek clamp invalid", "[decoder]") {
  auto path = std::filesystem::path(std::string(TEST_DATA_DIR) + "/sample.ogg");
  auto r = FileReader::open(path);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE(!(*dec)->seek(-5.0).has_value());
  REQUIRE((*dec)->seek(-5.0).error().code == Result::InvalidArg);
  auto bad = (*dec)->seek(std::numeric_limits<double>::infinity());
  REQUIRE(!bad.has_value());
}

TEST_CASE("decoder FileReader probe 32B with real wav", "[decoder]") {
  auto path = std::filesystem::path(std::string(TEST_DATA_DIR) + "/sample.wav");
  auto r = FileReader::open(path);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE((*dec)->sampleRate() > 0);
  REQUIRE((**r).tell() == 0);
}