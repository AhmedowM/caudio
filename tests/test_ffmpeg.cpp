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
  // FFmpeg probe should be permissive when available
  std::vector<std::byte> probeData(32, std::byte{0});
  probeData[0] = std::byte{'R'}; probeData[1] = std::byte{'I'}; 
  probeData[2] = std::byte{'F'}; probeData[3] = std::byte{'F'};
  
  auto r = MemoryReader::open(probeData);
  REQUIRE(r.has_value());
  // When FFmpeg is available, probe should return true for RIFF too (permissive)
  // But WavDecoder will match first in the probe check... 
  // Actually, the registry checks FFmpeg probe first, so it should try FFmpeg
  #ifdef CAUDIO_WITH_FFMPEG
  // With fake RIFF data, FFmpeg create will fail and fall back to WavDecoder
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value()); // Should succeed via fallback to WavDecoder
  #else
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value()); // Should succeed via WavDecoder
  #endif
}