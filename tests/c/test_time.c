#include <caudio.h>
#include <caudio/runtime/time.h>

#include "ca_test.h"

static int test_now(void) {
    ca_error err;
    ca_error_init(&err);

    ca_time_us_t t1, t2;
    CA_CHECK_EQ(CA_OK, ca_time_now(&t1, &err));
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK(t1 > 0);

    ca_time_sleep_us(50000, &err); /* 50 ms */
    CA_CHECK_EQ(CA_OK, err.code);

    CA_CHECK_EQ(CA_OK, ca_time_now(&t2, &err));
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK(t2 > t1);

    return ca_test_finish("time.now");
}

static int test_sleep(void) {
    ca_error err;
    ca_error_init(&err);

    ca_time_us_t t1, t2;
    CA_CHECK_EQ(CA_OK, ca_time_now(&t1, &err));

    /* sleep 100 ms, should take at least that long */
    CA_CHECK_EQ(CA_OK, ca_time_sleep_us(100000, &err));
    CA_CHECK_EQ(CA_OK, err.code);

    CA_CHECK_EQ(CA_OK, ca_time_now(&t2, &err));
    CA_CHECK(t2 - t1 >= 100000);

    return ca_test_finish("time.sleep");
}

static int test_now_null_err(void) {
    ca_time_us_t t;
    CA_CHECK_EQ(CA_OK, ca_time_now(&t, nullptr));
    CA_CHECK(t > 0);

    return ca_test_finish("time.now_null_err");
}

int main(void) {
    int failures = 0;
    failures += test_now();
    failures += test_sleep();
    failures += test_now_null_err();
    return failures;
}
