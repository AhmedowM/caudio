#include <cstdx/status.h>

#include <string.h>

#include "cx_test.h"

static_assert(sizeof(cx_status) == 1);

int main(void) {
    CX_CHECK_EQ(0, CX_OK);

    CX_CHECK(cx_status_is_ok(CX_OK));
    CX_CHECK(!cx_status_is_ok(CX_INVALID_ARGUMENT));
    CX_CHECK(!cx_status_is_ok(CX_OUT_OF_MEMORY));

    CX_CHECK(strcmp(cx_status_name(CX_OK), "CX_OK") == 0);
    CX_CHECK(strcmp(cx_status_name(CX_INVALID_ARGUMENT),
                     "CX_INVALID_ARGUMENT") == 0);
    CX_CHECK(strcmp(cx_status_name(CX_OUT_OF_MEMORY), "CX_OUT_OF_MEMORY") == 0);
    CX_CHECK(strcmp(cx_status_name(CX_INTERNAL), "CX_INTERNAL") == 0);

    auto unknown = (cx_status)200;

    CX_CHECK(strcmp(cx_status_name(unknown), "CX_UNKNOWN") == 0);

    return cx_test_finish("status");
}
