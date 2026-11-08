#include <caudio.h>
#include <caudio/runtime/cond.h>
#include <caudio/runtime/mutex.h>

#include "ca_test.h"

static int test_signal_wait(void) {
    ca_error err;
    ca_error_init(&err);

    ca_mutex_t m = nullptr;
    ca_cond_t c = nullptr;

    CA_CHECK_EQ(CA_OK, ca_mutex_create(&m, &err));
    CA_CHECK_EQ(CA_OK, ca_cond_create(&c, &err));

    int flag = 0;

    ca_mutex_lock(m, &err);
    flag = 1;
    CA_CHECK_EQ(CA_OK, ca_cond_signal(c, &err));
    ca_mutex_unlock(m, &err);

    ca_mutex_lock(m, &err);
    while (flag == 0) {
        CA_CHECK_EQ(CA_OK, ca_cond_wait(c, m, &err));
    }
    ca_mutex_unlock(m, &err);

    ca_mutex_destroy(m);
    ca_cond_destroy(c);

    return ca_test_finish("cond.signal_wait");
}

static int test_broadcast(void) {
    ca_error err;
    ca_error_init(&err);

    ca_mutex_t m = nullptr;
    ca_cond_t c = nullptr;

    CA_CHECK_EQ(CA_OK, ca_mutex_create(&m, &err));
    CA_CHECK_EQ(CA_OK, ca_cond_create(&c, &err));

    int flag = 0;

    ca_mutex_lock(m, &err);
    flag = 1;
    CA_CHECK_EQ(CA_OK, ca_cond_broadcast(c, &err));
    ca_mutex_unlock(m, &err);

    ca_mutex_lock(m, &err);
    while (flag == 0) {
        CA_CHECK_EQ(CA_OK, ca_cond_wait(c, m, &err));
    }
    ca_mutex_unlock(m, &err);

    ca_mutex_destroy(m);
    ca_cond_destroy(c);

    return ca_test_finish("cond.broadcast");
}

static int test_null_args(void) {
    ca_error err;
    ca_error_init(&err);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_cond_signal(nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_cond_broadcast(nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_cond_wait(nullptr, nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_cond_create(nullptr, &err));

    return ca_test_finish("cond.null_args");
}

int main(void) {
    int failures = 0;
    failures += test_signal_wait();
    failures += test_broadcast();
    failures += test_null_args();
    return failures;
}
