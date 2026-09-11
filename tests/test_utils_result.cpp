#include <catch2/catch_test_macros.hpp>

namespace caudio::utils::test {
bool result_toString_all();
bool result_enum_sequential();
bool result_noexcept_check();
} // namespace caudio::utils::test

using namespace caudio::utils::test;

TEST_CASE("StatusCode toString maps all codes", "[utils][StatusCode]") {
    REQUIRE(result_toString_all());
}

TEST_CASE("StatusCode enum values sequential 0..12", "[utils][StatusCode]") {
    REQUIRE(result_enum_sequential());
}

TEST_CASE("StatusCode toString noexcept", "[utils][StatusCode]") {
    REQUIRE(result_noexcept_check());
}






