#include <catch2/catch_test_macros.hpp>
import caudio.player;
import caudio.utils;

TEST_CASE("output callback no alloc") {
  caudio::utils::SpscRing<float> ring{8192, 2};
  auto out = caudio::player::AudioOutput::create({48000, 2, &ring, 1.0f});
  REQUIRE(out);
  std::array<float, 512> buf{};
  (*out)->testFill(buf);
  REQUIRE(buf[0] == 0.0f);
  REQUIRE(buf[511] == 0.0f);
}