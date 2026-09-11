#include <catch2/catch_test_macros.hpp>

namespace caudio::utils::test {
bool queue_busy_on_full();
bool queue_push_pop_fifo();
bool queue_wrap();
bool queue_mpsc_thread();
bool queue_10k_loop();
} // namespace caudio::utils::test

using namespace caudio::utils::test;

TEST_CASE("MpscQueue busy on full", "[utils][queue]") {
    REQUIRE(queue_busy_on_full());
}

TEST_CASE("MpscQueue push/pop FIFO and empty State", "[utils][queue]") {
    REQUIRE(queue_push_pop_fifo());
}

TEST_CASE("MpscQueue wrap-around", "[utils][queue]") {
    REQUIRE(queue_wrap());
}

TEST_CASE("MpscQueue MPSC thread producers", "[utils][queue]") {
    REQUIRE(queue_mpsc_thread());
}

TEST_CASE("MpscQueue 10k loop stress", "[utils][queue]") {
    REQUIRE(queue_10k_loop());
}






