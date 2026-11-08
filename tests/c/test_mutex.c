#include <caudio.h>
#include <caudio/runtime/mutex.h>

#include "ca_test.h"

static int test_lock_unlock(void) {
    ca_error err;
    ca_error_init(&err);

    ca_mutex_t m = nullptr;
    CA_CHECK_EQ(CA_OK, ca_mutex_create(&m, &err));
    CA_CHECK(m != nullptr);
    CA_CHECK_EQ(CA_OK, err.code);

    CA_CHECK_EQ(CA_OK, ca_mutex_lock(m, &err));
    CA_CHECK_EQ(CA_OK, ca_mutex_unlock(m, &err));

    ca_mutex_destroy(m);
    ca_mutex_destroy(nullptr); /* no-op */

    return ca_test_finish("mutex.lock_unlock");
}

static int test_trylock_busy(void) {
    ca_error err;
    ca_error_init(&err);

    ca_mutex_t m = nullptr;
    CA_CHECK_EQ(CA_OK, ca_mutex_create(&m, &err));

    CA_CHECK_EQ(CA_OK, ca_mutex_lock(m, &err));
    /* trylock on held mutex -> CA_BUSY */
    CA_CHECK_EQ(CA_BUSY, ca_mutex_trylock(m, &err));
    CA_CHECK_EQ(CA_OK, ca_mutex_unlock(m, &err));

    /* trylock on free mutex -> CA_OK */
    CA_CHECK_EQ(CA_OK, ca_mutex_trylock(m, &err));
    CA_CHECK_EQ(CA_OK, ca_mutex_unlock(m, &err));

    ca_mutex_destroy(m);

    return ca_test_finish("mutex.trylock_busy");
}

static int test_null_args(void) {
    ca_error err;
    ca_error_init(&err);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_mutex_lock(nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_mutex_unlock(nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_mutex_trylock(nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_mutex_create(nullptr, &err));

    return ca_test_finish("mutex.null_args");
}

int main(void) {
    int failures = 0;
    failures += test_lock_unlock();
    failures += test_trylock_busy();
    failures += test_null_args();
    return failures;
}
