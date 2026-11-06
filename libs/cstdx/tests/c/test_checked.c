#include <cstdx/checked.h>

#include <stdint.h>

#include "cx_test.h"

int main(void) {
    auto out = (size_t)0;

    CX_CHECK(!cx_add_overflow(1, 2, &out));
    CX_CHECK_EQ(3, out);

    CX_CHECK(cx_add_overflow(SIZE_MAX, 1, &out));
    CX_CHECK(cx_add_overflow(SIZE_MAX, SIZE_MAX, &out));

    CX_CHECK(!cx_mul_overflow(0, SIZE_MAX, &out));
    CX_CHECK_EQ(0, out);

    CX_CHECK(!cx_mul_overflow(7, 6, &out));
    CX_CHECK_EQ(42, out);

    CX_CHECK(cx_mul_overflow(SIZE_MAX, 2, &out));
    CX_CHECK(cx_mul_overflow(2, SIZE_MAX, &out));

    CX_CHECK(!cx_is_power_of_two(0));
    CX_CHECK(cx_is_power_of_two(1));
    CX_CHECK(cx_is_power_of_two(4096));
    CX_CHECK(!cx_is_power_of_two(3));
    CX_CHECK(!cx_is_power_of_two(4095));

    CX_CHECK(cx_align_up(0, 8, &out));
    CX_CHECK_EQ(0, out);

    CX_CHECK(cx_align_up(1, 8, &out));
    CX_CHECK_EQ(8, out);

    CX_CHECK(cx_align_up(8, 8, &out));
    CX_CHECK_EQ(8, out);

    CX_CHECK(cx_align_up(9, 8, &out));
    CX_CHECK_EQ(16, out);

    CX_CHECK(!cx_align_up(1, 0, &out));
    CX_CHECK(!cx_align_up(1, 3, &out));
    CX_CHECK(!cx_align_up(SIZE_MAX, 16, &out));

    return cx_test_finish("checked");
}
