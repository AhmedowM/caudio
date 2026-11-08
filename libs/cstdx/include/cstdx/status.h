#ifndef CSTDX_STATUS_H
#define CSTDX_STATUS_H

/**
 * @file status.h
 * @brief Status codes shared by all cstdx operations.
 *
 * Every fallible operation that does not return a value through a pointer
 * returns @ref cx_status by value. @ref CX_OK is zero, so `if (status)` is a
 * failure test.
 *
 * The enumerator values are part of the ABI: append new codes, never
 * renumber or remove an existing one.
 */

#include <cstdx/api.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result of a cstdx operation.
 *
 * @c uint8_t keeps the type one byte wide so it can be stored in containers
 * and packed into structures without padding concerns.
 */
typedef enum cx_status : uint8_t {
    /** The operation succeeded. */
    CX_OK = 0,

    /** A required pointer was nullptr, a size/alignment was invalid, etc. */
    CX_INVALID_ARGUMENT,

    /** The operation needed memory and the allocator could not provide it. */
    CX_OUT_OF_MEMORY,

    /** An arithmetic computation would not fit in @c size_t. */
    CX_OVERFLOW,

    /** An index or size was outside the valid range. */
    CX_OUT_OF_RANGE,

    /** The object was not in a state that permits the operation. */
    CX_INVALID_STATE,

    /** The requested item does not exist. */
    CX_NOT_FOUND,

    /** The operation is not supported by this implementation. */
    CX_UNSUPPORTED,

    /** An underlying I/O operation failed. */
    CX_IO,

    /** An invariant was violated. Indicates a bug in cstdx. */
    CX_INTERNAL
} cx_status;

/**
 * @brief Return a stable, human-readable name for a status code.
 *
 * The returned pointer has static storage duration and must not be freed.
 * Unknown values return the string "CX_UNKNOWN".
 */
[[nodiscard]] CX_API const char* cx_status_name(cx_status status);

/**
 * @brief Test whether a status represents success.
 */
[[nodiscard]] static inline bool cx_status_is_ok(cx_status status) { return status == CX_OK; }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_STATUS_H */
