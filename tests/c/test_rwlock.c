#include <caudio.h>
#include <caudio/runtime/rwlock.h>

#include "ca_test.h"

static int test_rdlock_wrlock(void) {
    ca_error err;
    ca_error_init(&err);

    ca_rwlock_t rw = nullptr;
    CA_CHECK_EQ(CA_OK, ca_rwlock_create(&rw, &err));

    /* multiple readers can hold concurrently */
    CA_CHECK_EQ(CA_OK, ca_rwlock_rdlock(rw, &err));
    CA_CHECK_EQ(CA_OK, ca_rwlock_rdlock(rw, &err));
    CA_CHECK_EQ(CA_OK, ca_rwlock_unlock(rw, &err));
    CA_CHECK_EQ(CA_OK, ca_rwlock_unlock(rw, &err));

    /* writer takes exclusive lock */
    CA_CHECK_EQ(CA_OK, ca_rwlock_wrlock(rw, &err));
    CA_CHECK_EQ(CA_OK, ca_rwlock_unlock(rw, &err));

    ca_rwlock_destroy(rw);

    return ca_test_finish("rwlock.rdlock_wrlock");
}

static int test_null_args(void) {
    ca_error err;
    ca_error_init(&err);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_rwlock_rdlock(nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_rwlock_wrlock(nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_rwlock_unlock(nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_rwlock_create(nullptr, &err));

    return ca_test_finish("rwlock.null_args");
}

int main(void) {
    int failures = 0;
    failures += test_rdlock_wrlock();
    failures += test_null_args();
    return failures;
}
