#ifndef CA_TEST_H
#define CA_TEST_H

/**
 * @file ca_test.h
 * @brief Minimal assertion helpers for the caudio C unit tests.
 *
 * Each test executable is a single translation unit with its own main(), so a
 * file-local counter is sufficient. No external test framework is required.
 */

#include <stdio.h>

static int ca_test_failures = 0;

#define CA_CHECK(cond)                                                        \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,            \
                    __LINE__, #cond);                                         \
            ca_test_failures++;                                               \
        }                                                                     \
    } while (0)

#define CA_CHECK_EQ(expected, actual)                                         \
    do {                                                                      \
        long long ca_expected = (long long)(expected);                        \
        long long ca_actual = (long long)(actual);                            \
        if (ca_expected != ca_actual) {                                       \
            fprintf(stderr, "%s:%d: expected %lld, got %lld\n", __FILE__,     \
                    __LINE__, ca_expected, ca_actual);                        \
            ca_test_failures++;                                               \
        }                                                                     \
    } while (0)

static inline int ca_test_finish(const char *name) {
    if (ca_test_failures == 0) {
        printf("%s: ok\n", name);
        return 0;
    }

    fprintf(stderr, "%s: %d failure(s)\n", name, ca_test_failures);
    return 1;
}

#endif /* CA_TEST_H */
