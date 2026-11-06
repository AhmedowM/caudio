/*
 * C23 header self-containment smoke test.
 *
 * Every public header is included directly (not via the umbrella header) so a
 * missing include or a C++-incompatible construct is caught in CI.
 */

#include <cstdx/api.h>
#include <cstdx/allocator.h>
#include <cstdx/allocator_canary.h>
#include <cstdx/allocator_counted.h>
#include <cstdx/arena.h>
#include <cstdx/base64.h>
#include <cstdx/bytes.h>
#include <cstdx/checked.h>
#include <cstdx/cstdx.h>
#include <cstdx/error.h>
#include <cstdx/expected.h>
#include <cstdx/hash.h>
#include <cstdx/pool.h>
#include <cstdx/result.h>
#include <cstdx/slice.h>
#include <cstdx/sort.h>
#include <cstdx/status.h>
#include <cstdx/string.h>
#include <cstdx/string_builder.h>
#include <cstdx/string_view.h>
#include <cstdx/vector.h>

int main(void) {
    return 0;
}
