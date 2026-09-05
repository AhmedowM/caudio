#include <catch2/catch_test_macros.hpp>

namespace caudio::utils::test {
bool log_injected();
bool log_level_filter();
bool log_convenience();
bool log_set_callback_level();
bool log_null_safe();
bool log_toString_level();
} // namespace caudio::utils::test

using namespace caudio::utils::test;

TEST_CASE("Logger injected callback", "[utils][log]") {
    REQUIRE(log_injected());
}

TEST_CASE("Logger level filtering", "[utils][log]") {
    REQUIRE(log_level_filter());
}

TEST_CASE("Logger convenience methods", "[utils][log]") {
    REQUIRE(log_convenience());
}

TEST_CASE("Logger setCallback and setLevel", "[utils][log]") {
    REQUIRE(log_set_callback_level());
}

TEST_CASE("Logger null callback safe", "[utils][log]") {
    REQUIRE(log_null_safe());
}

TEST_CASE("Logger toString Level", "[utils][log]") {
    REQUIRE(log_toString_level());
}
