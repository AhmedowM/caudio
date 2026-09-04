#include <catch2/catch_test_macros.hpp>

namespace caudio::utils::test {
bool ring_write_read_wrap();
bool ring_basic_mono();
bool ring_channels();
bool ring_wrap();
bool ring_truncation();
bool ring_available_reset();
bool ring_10k_loop();
bool ring_concurrent_spsc();
} // namespace caudio::utils::test

using namespace caudio::utils::test;

TEST_CASE("SpscRing write/read wrap", "[utils][ring]") {
  REQUIRE(ring_write_read_wrap());
}

TEST_CASE("SpscRing basic SPSC mono", "[utils][ring]") {
  REQUIRE(ring_basic_mono());
}

TEST_CASE("SpscRing channel counts stereo/quad", "[utils][ring]") {
  REQUIRE(ring_channels());
}

TEST_CASE("SpscRing wrap-around", "[utils][ring]") {
  REQUIRE(ring_wrap());
}

TEST_CASE("SpscRing truncation over-read/write", "[utils][ring]") {
  REQUIRE(ring_truncation());
}

TEST_CASE("SpscRing available and reset", "[utils][ring]") {
  REQUIRE(ring_available_reset());
}

TEST_CASE("SpscRing 10k loop stress", "[utils][ring]") {
  REQUIRE(ring_10k_loop());
}

TEST_CASE("SpscRing concurrent SPSC", "[utils][ring]") {
  REQUIRE(ring_concurrent_spsc());
}
