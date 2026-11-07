/*
 * C++23 header compatibility smoke test.
 *
 * The C++ differential harness includes these headers, so they must compile as
 * C++23 as well as C23.
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
#include <cstdx/version.h>

int main() {
    return 0;
}
