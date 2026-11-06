#include <caudio.h>
#include <caudio/runtime/path.h>

#include <string.h>

#include "ca_test.h"

static int test_join(void) {
    char out[256];
    ca_error err;
    ca_error_init(&err);

    size_t n = ca_path_join("foo/bar", "baz.txt", out, sizeof(out), &err);
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK_EQ(15, n);
    CA_CHECK_EQ(0, strcmp(out, "foo/bar/baz.txt"));

    /* base with trailing separator: no extra sep added */
    n = ca_path_join("foo/", "bar", out, sizeof(out), &err);
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK_EQ(7, n);
    CA_CHECK_EQ(0, strcmp(out, "foo/bar"));

    /* empty base */
    n = ca_path_join("", "bar", out, sizeof(out), &err);
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK_EQ(3, n);
    CA_CHECK_EQ(0, strcmp(out, "bar"));

    /* buffer too small */
    n = ca_path_join("foo/bar", "baz.txt", out, 4, &err);
    CA_CHECK_EQ(CA_OVERFLOW, err.code);
    CA_CHECK_EQ((size_t)-1, n);

    /* NULL args */
    n = ca_path_join(nullptr, "bar", out, sizeof(out), &err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, err.code);
    CA_CHECK_EQ((size_t)-1, n);

    n = ca_path_join("foo", nullptr, out, sizeof(out), &err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, err.code);
    CA_CHECK_EQ((size_t)-1, n);

    n = ca_path_join("foo", "bar", nullptr, 10, &err);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, err.code);
    CA_CHECK_EQ((size_t)-1, n);

    return ca_test_finish("path.join");
}

static int test_basename(void) {
    CA_CHECK_EQ(0, strcmp(ca_path_basename("/usr/local/bin/caudio"), "caudio"));
    CA_CHECK_EQ(0, strcmp(ca_path_basename("caudio"), "caudio"));
    CA_CHECK_EQ(0, strcmp(ca_path_basename("/usr/local"), "local"));
    CA_CHECK_EQ(0, strcmp(ca_path_basename("/"), "/"));
    CA_CHECK(nullptr == ca_path_basename(nullptr));

    return ca_test_finish("path.basename");
}

static int test_normalize(void) {
    char p[64] = "C:\\Users\\Admin\\caudio";
    ca_path_normalize(p);
    CA_CHECK_EQ(0, strcmp(p, "C:/Users/Admin/caudio"));

    char p2[64] = "a/b\\c";
    ca_path_normalize(p2);
    CA_CHECK_EQ(0, strcmp(p2, "a/b/c"));

    char p3[64] = "already/forward";
    ca_path_normalize(p3);
    CA_CHECK_EQ(0, strcmp(p3, "already/forward"));

    CA_CHECK(nullptr == ca_path_normalize(nullptr));

    return ca_test_finish("path.normalize");
}

int main(void) {
    int failures = 0;
    failures += test_join();
    failures += test_basename();
    failures += test_normalize();
    return failures;
}
