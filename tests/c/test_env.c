#include <caudio.h>
#include <caudio/runtime/env.h>
#include <string.h>

#include "ca_test.h"

#define ENV_NAME "CAUDIO_TEST_ENV_A"
#define ENV_NAME_B "CAUDIO_TEST_ENV_B"
#define ENV_NAME_UTF8 "CAUDIO_TEST_ENV_UTF8"

static void cleanup(const char* name) { (void)ca_env_unset(name, nullptr); }

static int test_get_missing(void) {
    ca_error err;
    ca_error_init(&err);

    cleanup(ENV_NAME);

    char buffer[32];
    memset(buffer, 'x', sizeof(buffer));
    size_t len = 12345;

    /* Buffer provided: missing values clear the destination and report 0. */
    CA_CHECK_EQ(CA_NOT_FOUND, ca_env_get(ENV_NAME, buffer, sizeof(buffer), &len, &err));
    CA_CHECK_EQ(CA_NOT_FOUND, err.code);
    CA_CHECK_EQ(0, len);
    CA_CHECK_EQ('\0', buffer[0]);

    /* Length query for a missing value is also CA_NOT_FOUND. */
    CA_CHECK_EQ(CA_NOT_FOUND, ca_env_get(ENV_NAME, nullptr, 0, &len, &err));
    CA_CHECK_EQ(0, len);

    cleanup(ENV_NAME);
    return ca_test_finish("env.get_missing");
}

static int test_set_get(void) {
    ca_error err;
    ca_error_init(&err);

    cleanup(ENV_NAME);

    CA_CHECK_EQ(CA_OK, ca_env_set(ENV_NAME, "hello", true, &err));
    CA_CHECK_EQ(CA_OK, err.code);

    char buffer[32];
    size_t len = 0;

    CA_CHECK_EQ(CA_OK, ca_env_get(ENV_NAME, buffer, sizeof(buffer), &len, &err));
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK_EQ(5, len);
    CA_CHECK_EQ(0, strcmp(buffer, "hello"));

    /* A nullptr out_len is allowed. */
    CA_CHECK_EQ(CA_OK, ca_env_get(ENV_NAME, buffer, sizeof(buffer), nullptr, nullptr));
    CA_CHECK_EQ(0, strcmp(buffer, "hello"));

    cleanup(ENV_NAME);
    return ca_test_finish("env.set_get");
}

static int test_query_length(void) {
    ca_error err;
    ca_error_init(&err);

    cleanup(ENV_NAME);

    CA_CHECK_EQ(CA_OK, ca_env_set(ENV_NAME, "query-me", true, &err));

    size_t len = 0;
    CA_CHECK_EQ(CA_OK, ca_env_get(ENV_NAME, nullptr, 0, &len, &err));
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK_EQ(8, len);

    /* Exact fit: len bytes plus the NUL. */
    char exact[9];
    memset(exact, 'x', sizeof(exact));
    len = 0;
    CA_CHECK_EQ(CA_OK, ca_env_get(ENV_NAME, exact, sizeof(exact), &len, &err));
    CA_CHECK_EQ(8, len);
    CA_CHECK_EQ(0, strcmp(exact, "query-me"));
    CA_CHECK_EQ('\0', exact[8]);

    cleanup(ENV_NAME);
    return ca_test_finish("env.query_length");
}

static int test_buffer_too_small(void) {
    ca_error err;
    ca_error_init(&err);

    cleanup(ENV_NAME);

    CA_CHECK_EQ(CA_OK, ca_env_set(ENV_NAME, "too-long", true, &err));

    char buffer[8];
    memset(buffer, 'x', sizeof(buffer));
    size_t len = 0;

    CA_CHECK_EQ(CA_OVERFLOW, ca_env_get(ENV_NAME, buffer, sizeof(buffer), &len, &err));
    CA_CHECK_EQ(CA_OVERFLOW, err.code);

    /* The required length is reported even on overflow. */
    CA_CHECK_EQ(8, len);
    CA_CHECK_EQ('\0', buffer[0]);

    /* One byte short of len + 1: still overflow. */
    char one_short[8];
    len = 0;
    CA_CHECK_EQ(CA_OVERFLOW, ca_env_get(ENV_NAME, one_short, sizeof(one_short), &len, &err));
    CA_CHECK_EQ('\0', one_short[0]);

    cleanup(ENV_NAME);
    return ca_test_finish("env.buffer_too_small");
}

static int test_overwrite_semantics(void) {
    ca_error err;
    ca_error_init(&err);

    cleanup(ENV_NAME);

    CA_CHECK_EQ(CA_OK, ca_env_set(ENV_NAME, "first", true, &err));

    /* overwrite = false on an existing variable fails and leaves it alone. */
    CA_CHECK_EQ(CA_ALREADY_EXISTS, ca_env_set(ENV_NAME, "second", false, &err));

    char buffer[32];
    size_t len = 0;
    CA_CHECK_EQ(CA_OK, ca_env_get(ENV_NAME, buffer, sizeof(buffer), &len, &err));
    CA_CHECK_EQ(0, strcmp(buffer, "first"));

    /* overwrite = true replaces the value. */
    CA_CHECK_EQ(CA_OK, ca_env_set(ENV_NAME, "second", true, &err));
    CA_CHECK_EQ(CA_OK, ca_env_get(ENV_NAME, buffer, sizeof(buffer), &len, &err));
    CA_CHECK_EQ(0, strcmp(buffer, "second"));

    /* overwrite = false on a fresh variable succeeds. */
    cleanup(ENV_NAME_B);
    CA_CHECK_EQ(CA_OK, ca_env_set(ENV_NAME_B, "fresh", false, &err));
    CA_CHECK_EQ(CA_OK, ca_env_get(ENV_NAME_B, buffer, sizeof(buffer), &len, &err));
    CA_CHECK_EQ(0, strcmp(buffer, "fresh"));

    cleanup(ENV_NAME);
    cleanup(ENV_NAME_B);
    return ca_test_finish("env.overwrite");
}

static int test_unset(void) {
    ca_error err;
    ca_error_init(&err);

    cleanup(ENV_NAME);

    CA_CHECK_EQ(CA_OK, ca_env_set(ENV_NAME, "delete-me", true, &err));

    char buffer[32];
    size_t len = 0;
    CA_CHECK_EQ(CA_OK, ca_env_get(ENV_NAME, buffer, sizeof(buffer), &len, &err));

    CA_CHECK_EQ(CA_OK, ca_env_unset(ENV_NAME, &err));
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK_EQ(CA_NOT_FOUND, ca_env_get(ENV_NAME, buffer, sizeof(buffer), &len, &err));

    /* Removing an unset variable is idempotent and not an error. */
    CA_CHECK_EQ(CA_OK, ca_env_unset(ENV_NAME, &err));
    CA_CHECK_EQ(CA_OK, err.code);

    cleanup(ENV_NAME);
    return ca_test_finish("env.unset");
}

static int test_empty_value(void) {
    ca_error err;
    ca_error_init(&err);

    cleanup(ENV_NAME);

    CA_CHECK_EQ(CA_OK, ca_env_set(ENV_NAME, "", true, &err));

    size_t len = 12345;
    CA_CHECK_EQ(CA_OK, ca_env_get(ENV_NAME, nullptr, 0, &len, &err));
    CA_CHECK_EQ(0, len);

    char buffer[4];
    memset(buffer, 'x', sizeof(buffer));
    len = 12345;
    CA_CHECK_EQ(CA_OK, ca_env_get(ENV_NAME, buffer, sizeof(buffer), &len, &err));
    CA_CHECK_EQ(0, len);
    CA_CHECK_EQ('\0', buffer[0]);

    /* An empty value still counts as present for overwrite = false. */
    CA_CHECK_EQ(CA_ALREADY_EXISTS, ca_env_set(ENV_NAME, "nonempty", false, &err));

    cleanup(ENV_NAME);
    return ca_test_finish("env.empty_value");
}

static int test_utf8_roundtrip(void) {
    ca_error err;
    ca_error_init(&err);

    cleanup(ENV_NAME_UTF8);

    const char* value = "caf\xC3\xA9 \xE4\xB8\x96\xE7\x95\x8C";
    size_t expected_len = strlen(value);
    CA_CHECK(expected_len > 0);

    CA_CHECK_EQ(CA_OK, ca_env_set(ENV_NAME_UTF8, value, true, &err));

    char buffer[64];
    size_t len = 0;
    CA_CHECK_EQ(CA_OK, ca_env_get(ENV_NAME_UTF8, buffer, sizeof(buffer), &len, &err));
    CA_CHECK_EQ((long long)expected_len, (long long)len);
    CA_CHECK_EQ(0, memcmp(buffer, value, expected_len + 1));

    cleanup(ENV_NAME_UTF8);
    return ca_test_finish("env.utf8_roundtrip");
}

static int test_invalid_args(void) {
    ca_error err;
    ca_error_init(&err);

    char buffer[8];
    size_t len = 0;

    /* nullptr or empty names, and names containing '='. */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_env_get(nullptr, buffer, sizeof(buffer), &len, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_env_get("", buffer, sizeof(buffer), &len, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_env_get("A=B", buffer, sizeof(buffer), &len, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_env_set(nullptr, "x", true, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_env_set("", "x", true, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_env_set("A=B", "x", true, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_env_unset(nullptr, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_env_unset("", &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_env_unset("A=B", &err));

    /* nullptr value for set. */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_env_set(ENV_NAME, nullptr, true, &err));

    /* out / out_size mismatch. */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_env_get(ENV_NAME, nullptr, 8, &len, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_env_get(ENV_NAME, buffer, 0, &len, &err));

    /* err == nullptr is accepted on every entry point. */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_env_get(nullptr, nullptr, 0, nullptr, nullptr));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_env_set(nullptr, nullptr, true, nullptr));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_env_unset(nullptr, nullptr));

    return ca_test_finish("env.invalid_args");
}

int main(void) {
    int failures = 0;

    failures += test_get_missing();
    failures += test_set_get();
    failures += test_query_length();
    failures += test_buffer_too_small();
    failures += test_overwrite_semantics();
    failures += test_unset();
    failures += test_empty_value();
    failures += test_utf8_roundtrip();
    failures += test_invalid_args();

    return failures;
}
