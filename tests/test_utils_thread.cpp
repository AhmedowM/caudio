#include <catch2/catch_test_macros.hpp>

namespace caudio::utils::test {
bool thread_sleep_timing();
bool thread_jthread_basic();
bool thread_parallel_10();
bool thread_setname_current();
bool thread_setname_jthread();
bool thread_100_stress();
bool thread_sleepForMs();
} // namespace caudio::utils::test

using namespace caudio::utils::test;

TEST_CASE("Thread sleepFor timing", "[utils][thread]") {
    REQUIRE(thread_sleep_timing());
}

TEST_CASE("Thread jthread creation and join", "[utils][thread]") {
    REQUIRE(thread_jthread_basic());
}

TEST_CASE("Thread parallel 10 jthreads", "[utils][thread]") {
    REQUIRE(thread_parallel_10());
}

TEST_CASE("Thread setThreadName current", "[utils][thread]") {
    REQUIRE(thread_setname_current());
}

TEST_CASE("Thread setThreadName via jthread", "[utils][thread]") {
    REQUIRE(thread_setname_jthread());
}

TEST_CASE("Thread 100 stress", "[utils][thread]") {
    REQUIRE(thread_100_stress());
}

TEST_CASE("Thread sleepForMs wrapper", "[utils][thread]") {
    REQUIRE(thread_sleepForMs());
}
