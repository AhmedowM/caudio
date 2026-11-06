#include <caudio.h>
#include <caudio/runtime/tls.h>

#include <string.h>

#include "ca_test.h"

static int test_set_get(void) {
    ca_error err;
    ca_error_init(&err);

    ca_tls_key_t key = nullptr;
    CA_CHECK_EQ(CA_OK, ca_tls_key_create(&key, nullptr, &err));

    const char *val = "tls-value-42";
    CA_CHECK_EQ(CA_OK, ca_tls_set(key, (void *)val, &err));

    void *got = ca_tls_get(key, &err);
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK_EQ(0, strcmp((const char *)got, val));

    ca_tls_key_destroy(key);

    return ca_test_finish("tls.set_get");
}

static int test_null_args(void) {
    ca_error err;
    ca_error_init(&err);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_tls_set(nullptr, (void *)"x", &err));
    CA_CHECK(nullptr == ca_tls_get(nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_tls_key_create(nullptr, nullptr, &err));

    return ca_test_finish("tls.null_args");
}

int main(void) {
    int failures = 0;
    failures += test_set_get();
    failures += test_null_args();
    return failures;
}
