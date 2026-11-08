#include <cstdx/result.h>

#include "cx_test.h"

int main(void) {
    cx_result success = cx_result_success();

    CX_CHECK(cx_result_is_success(&success));
    CX_CHECK(!cx_result_is_failure(&success));
    CX_CHECK(cx_result_error(&success) == nullptr);

    cx_error error = {
        .code = 7,
        .message = "boom",
    };
    cx_result failure = cx_result_failure(error);

    CX_CHECK(!cx_result_is_success(&failure));
    CX_CHECK(cx_result_is_failure(&failure));

    const cx_error *stored = cx_result_error(&failure);

    CX_CHECK(stored != nullptr);
    CX_CHECK_EQ(7, stored->code);
    CX_CHECK(stored->message == error.message);

    CX_CHECK(!cx_result_is_success(nullptr));
    CX_CHECK(!cx_result_is_failure(nullptr));
    CX_CHECK(cx_result_error(nullptr) == nullptr);

    return cx_test_finish("result");
}
