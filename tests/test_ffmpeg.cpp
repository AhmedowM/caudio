#include <catch2/catch_test_macros.hpp>
#include <span>
#include <array>
#include <filesystem>
#include <vector>
#include <cstring>

import caudio.player;
import caudio.utils;

using namespace caudio::player;
using namespace caudio::utils;

TEST_CASE("ffmpeg primary decodes m4a", "[ffmpeg]") {
  auto r = FileReader::open("tests/fixtures/sample.m4a");
  if (!r) {
    SKIP("no fixture sample.m4a");
  }
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  // Should be FfmpegDecoder when FFmpeg present
  std::array<float, 1024> out{};
  REQUIRE((*dec)->decode(out) > 0);
}

TEST_CASE("ffmpeg decodes real ogg file", "[ffmpeg]") {
  auto path = std::filesystem::path(TEST_DATA_DIR) / "sample.ogg";
  if (!std::filesystem::exists(path)) {
    SKIP("no fixture sample.ogg");
  }
  auto r = FileReader::open(path);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  std::array<float, 1024> out{};
  REQUIRE((*dec)->decode(out) > 0);
}

TEST_CASE("ffmpeg probe returns true for any data when available", "[ffmpeg]") {
  // FFmpeg probe is permissive (returns true for any data >= 4 bytes)
  std::vector<std::byte> probeData(32, std::byte{0});
  probeData[0] = std::byte{'R'}; probeData[1] = std::byte{'I'}; 
  probeData[2] = std::byte{'F'}; probeData[3] = std::byte{'F'};
  
  auto r = MemoryReader::open(probeData);
  REQUIRE(r.has_value());
  
  #ifdef CAUDIO_WITH_FFMPEG
  // With fake data, FFmpeg probe returns true but create fails -> unsupported
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(!dec.has_value());
  REQUIRE(dec.error().code == Result::Unsupported);
  #else
  // Without FFmpeg, no decoder available
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(!dec.has_value());
  #endif
}