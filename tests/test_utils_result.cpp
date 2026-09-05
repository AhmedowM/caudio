#include <catch2/catch_test_macros.hpp>

namespace caudio::utils::test {
bool result_toString_all();
bool result_enum_sequential();
bool result_noexcept_check();
} // namespace caudio::utils::test

using namespace caudio::utils::test;

TEST_CASE("Result toString maps all codes", "[utils][result]") {
    REQUIRE(result_toString_all());
}

TEST_CASE("Result enum values sequential 0..12", "[utils][result]") {
    REQUIRE(result_enum_sequential());
}

TEST_CASE("Result toString noexcept", "[utils][result]") {
    REQUIRE(result_noexcept_check());
}
