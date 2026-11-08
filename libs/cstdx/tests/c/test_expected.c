#include <cstdx/expected.h>

#include "cx_test.h"

int main(void) {
    int value = 42;
    cx_expected has_value = cx_expected_from_value(&value);

    CX_CHECK(cx_expected_has_value(&has_value));
    CX_CHECK(!cx_expected_has_error(&has_value));
    CX_CHECK(cx_expected_value(&has_value) == &value);
    CX_CHECK(cx_expected_error(&has_value) == nullptr);

    /* A value that is intentionally nullptr is still a value. */
    cx_expected null_value = cx_expected_from_value(nullptr);

    CX_CHECK(cx_expected_has_value(&null_value));
    CX_CHECK(cx_expected_value(&null_value) == nullptr);

    cx_error error = {
        .code = 9,
        .message = "bad",
    };
    cx_expected has_error = cx_expected_from_error(error);

    CX_CHECK(!cx_expected_has_value(&has_error));
    CX_CHECK(cx_expected_has_error(&has_error));
    CX_CHECK(cx_expected_value(&has_error) == nullptr);

    const cx_error *stored = cx_expected_error(&has_error);

    CX_CHECK(stored != nullptr);
    CX_CHECK_EQ(9, stored->code);
    CX_CHECK(stored->message == error.message);

    CX_CHECK(!cx_expected_has_value(nullptr));
    CX_CHECK(!cx_expected_has_error(nullptr));
    CX_CHECK(cx_expected_value(nullptr) == nullptr);
    CX_CHECK(cx_expected_error(nullptr) == nullptr);

    return cx_test_finish("expected");
}
