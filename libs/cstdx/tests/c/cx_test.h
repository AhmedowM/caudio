#ifndef CSTDX_TEST_H
#define CSTDX_TEST_H

/**
 * @file cx_test.h
 * @brief Minimal assertion helpers for the cstdx C unit tests.
 *
 * Each test executable is a single translation unit with its own main(), so a
 * file-local counter is sufficient. No external test framework is required.
 */

#include <stdio.h>

static int cx_test_failures = 0;

#define CX_CHECK(cond)                                                      \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,          \
                    __LINE__, #cond);                                       \
            cx_test_failures++;                                             \
        }                                                                   \
    } while (0)

#define CX_CHECK_EQ(expected, actual)                                       \
    do {                                                                    \
        long long cx_expected = (long long)(expected);                       \
        long long cx_actual = (long long)(actual);                           \
        if (cx_expected != cx_actual) {                                      \
            fprintf(stderr, "%s:%d: expected %lld, got %lld\n", __FILE__,    \
                    __LINE__, cx_expected, cx_actual);                       \
            cx_test_failures++;                                              \
        }                                                                    \
    } while (0)

static inline int cx_test_finish(const char *name) {
    if (cx_test_failures == 0) {
        printf("%s: ok\n", name);
        return 0;
    }

    fprintf(stderr, "%s: %d failure(s)\n", name, cx_test_failures);
    return 1;
}

#endif /* CSTDX_TEST_H */
