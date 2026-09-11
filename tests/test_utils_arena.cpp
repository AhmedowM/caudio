#include <catch2/catch_test_macros.hpp>

namespace caudio::utils::test {
bool arena_basic();
bool arena_reset();
bool arena_exhaustion();
bool arena_alignment();
bool arena_zero_cap();
bool arena_64b_align();
bool arena_interleaved();
bool arena_default_64k();
bool arena_zero_alloc();
} // namespace caudio::utils::test

using namespace caudio::utils::test;

TEST_CASE("Arena create/destroy and basic alloc", "[utils][arena]") {
    REQUIRE(arena_basic());
}

TEST_CASE("Arena reset returns to start", "[utils][arena]") {
    REQUIRE(arena_reset());
}

TEST_CASE("Arena exhaustion", "[utils][arena]") {
    REQUIRE(arena_exhaustion());
}

TEST_CASE("Arena alignment 1..64", "[utils][arena]") {
    REQUIRE(arena_alignment());
}

TEST_CASE("Arena zero capacity", "[utils][arena]") {
    REQUIRE(arena_zero_cap());
}

TEST_CASE("Arena 64B base alignment", "[utils][arena]") {
    REQUIRE(arena_64b_align());
}

TEST_CASE("Arena interleaved alignments", "[utils][arena]") {
    REQUIRE(arena_interleaved());
}

TEST_CASE("Arena default 64K capacity", "[utils][arena]") {
    REQUIRE(arena_default_64k());
}

TEST_CASE("Arena allocate zero", "[utils][arena]") {
    REQUIRE(arena_zero_alloc());
}






