#include <cstdx/version.h>
#include <string.h>

#include "cx_test.h"

static_assert(CSTDX_VERSION_MAJOR >= 0);
static_assert(CSTDX_VERSION_MINOR >= 0);
static_assert(CSTDX_VERSION_PATCH >= 0);

int main(void) {
    CX_CHECK(strlen(CSTDX_VERSION) > 0);
    CX_CHECK(CSTDX_VERSION[0] >= '0' && CSTDX_VERSION[0] <= '9');
    CX_CHECK(strlen(CSTDX_GIT_HASH) > 0);

    return cx_test_finish("version");
}
