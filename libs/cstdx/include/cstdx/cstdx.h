#ifndef CSTDX_CSTDX_H
#define CSTDX_CSTDX_H

/**
 * @file cstdx.h
 * @brief Umbrella header for the cstdx library.
 *
 * Include this for the whole library, or include the individual headers for
 * tighter dependencies.
 */

#include <cstdx/api.h>

#include <cstdx/status.h>
#include <cstdx/checked.h>

#include <cstdx/error.h>
#include <cstdx/result.h>
#include <cstdx/expected.h>

#include <cstdx/allocator.h>
#include <cstdx/arena.h>
#include <cstdx/pool.h>
#include <cstdx/allocator_counted.h>
#include <cstdx/allocator_canary.h>

#include <cstdx/slice.h>
#include <cstdx/bytes.h>
#include <cstdx/string_view.h>
#include <cstdx/string.h>
#include <cstdx/string_builder.h>

#include <cstdx/vector.h>

#include <cstdx/hash.h>
#include <cstdx/sort.h>
#include <cstdx/base64.h>

#endif /* CSTDX_CSTDX_H */
