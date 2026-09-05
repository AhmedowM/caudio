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

static std::vector<std::byte> makeProbe(const char* sig, std::size_t n = 32) {
  std::vector<std::byte> v(n, std::byte{0});
  if (sig) {
    std::size_t len = std::strlen(sig);
    for (std::size_t i = 0; i < len && i < n; ++i) v[i] = static_cast<std::byte>(sig[i]);
  }
  return v;
}

TEST_CASE("decode registry probe 32B wav", "[decoder]") {
  auto sig = makeProbe("RIFF");
  auto r = MemoryReader::open(sig);
  REQUIRE(r.has_value());
  int64_t before = (**r).tell();
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE((*dec)->sampleRate() == 8000);
  REQUIRE((*dec)->channels() == 1);
  REQUIRE((*dec)->totalFrames() == 8000);
  REQUIRE((**r).tell() == before);
}

TEST_CASE("decode registry probe 32B flac", "[decoder]") {
  auto sig = makeProbe("fLaC");
  auto r = MemoryReader::open(sig);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE((*dec)->sampleRate() == 44100);
  REQUIRE((*dec)->channels() == 2);
}

TEST_CASE("decode registry probe 32B mp3 id3", "[decoder]") {
  auto sig = makeProbe("ID3");
  auto r = MemoryReader::open(sig);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE((*dec)->sampleRate() == 48000);
  REQUIRE((*dec)->channels() == 2);
}

TEST_CASE("decode registry probe 32B mp3 sync", "[decoder]") {
  std::vector<std::byte> v(32, std::byte{0});
  v[0] = std::byte{0xFF};
  v[1] = std::byte{0xFB};
  auto r = MemoryReader::open(v);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE((*dec)->sampleRate() == 48000);
}

TEST_CASE("decode registry probe 32B vorbis", "[decoder]") {
  auto sig = makeProbe("OggS");
  auto r = MemoryReader::open(sig);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE((*dec)->sampleRate() == 22050);
  REQUIRE((*dec)->channels() == 1);
}

TEST_CASE("decode registry probe unsupported", "[decoder]") {
  auto sig = makeProbe("XXXX");
  auto r = MemoryReader::open(sig);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(!dec.has_value());
  REQUIRE(dec.error().code == Result::Unsupported);
}

TEST_CASE("decode registry probe restore offset", "[decoder]") {
  auto sig = makeProbe("RIFF");
  sig.resize(64, std::byte{0});
  auto r = MemoryReader::open(sig);
  REQUIRE(r.has_value());
  REQUIRE((**r).seek(10, SEEK_SET).has_value());
  REQUIRE((**r).tell() == 10);
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE((**r).tell() == 10);
}

TEST_CASE("decode registry order wav before others", "[decoder]") {
  auto sig = makeProbe("RIFF");
  auto r = MemoryReader::open(sig);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE((*dec)->sampleRate() == 8000);
}

TEST_CASE("miniaudio decoder sine fallback wav", "[decoder]") {
  auto sig = makeProbe("RIFF");
  auto r = MemoryReader::open(sig);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  std::array<float, 16> out{};
  std::size_t got = (*dec)->decode(out);
  REQUIRE(got > 0);
  bool nonZero = false;
  for (float f : out) if (std::abs(f) > 1e-6f) nonZero = true;
  REQUIRE(nonZero);
}

TEST_CASE("miniaudio decoder sine fallback flac", "[decoder]") {
  auto sig = makeProbe("fLaC");
  auto r = MemoryReader::open(sig);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE((*dec)->sampleRate() == 44100);
  REQUIRE((*dec)->channels() == 2);
  std::array<float, 8> out{};
  std::size_t got = (*dec)->decode(out);
  REQUIRE(got > 0);
}

TEST_CASE("miniaudio decoder sine fallback mp3", "[decoder]") {
  auto sig = makeProbe("ID3");
  auto r = MemoryReader::open(sig);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE((*dec)->sampleRate() == 48000);
  std::array<float, 8> out{};
  REQUIRE((*dec)->decode(out) > 0);
}

TEST_CASE("miniaudio decoder sine fallback vorbis", "[decoder]") {
  auto sig = makeProbe("OggS");
  auto r = MemoryReader::open(sig);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE((*dec)->sampleRate() == 22050);
  std::array<float, 4> out{};
  REQUIRE((*dec)->decode(out) > 0);
  REQUIRE((*dec)->seek(0.5).has_value());
  REQUIRE(!(*dec)->seek(-1.0).has_value());
}

TEST_CASE("miniaudio decoder ogg real", "[decoder]") {
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

TEST_CASE("decoder seek clamp invalid", "[decoder]") {
  auto sig = makeProbe("RIFF");
  auto r = MemoryReader::open(sig);
  REQUIRE(r.has_value());
  auto dec = DecoderRegistry::open(**r);
  REQUIRE(dec.has_value());
  REQUIRE(!(*dec)->seek(-5.0).has_value());
  REQUIRE((*dec)->seek(-5.0).error().code == Result::InvalidArg);
  auto bad = (*dec)->seek(std::numeric_limits<double>::infinity());
  REQUIRE(!bad.has_value());
}

TEST_CASE("decoder FileReader probe 32B", "[decoder]") {
  auto dir = std::filesystem::temp_directory_path() / "caudio_decoder_tests";
  std::filesystem::create_directories(dir);
  auto path = dir / "probe_riff.wav";
  {
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF----WAVE", 12);
    std::vector<char> pad(20, 0);
    out.write(pad.data(), static_cast<std::streamsize>(pad.size()));
  }
  {
    auto r = FileReader::open(path);
    REQUIRE(r.has_value());
    auto dec = DecoderRegistry::open(**r);
    REQUIRE(dec.has_value());
    REQUIRE((*dec)->sampleRate() == 8000);
    REQUIRE((**r).tell() == 0);
  }
  std::filesystem::remove(path);
}