#ifndef CSTDX_ALLOCATOR_CANARY_H
#define CSTDX_ALLOCATOR_CANARY_H

/**
 * @file allocator_canary.h
 * @brief Canary allocator decorator (guard bytes).
 *
 * A canary allocator wraps an inner allocator, places a guard region before
 * and after every block, and verifies the guard regions when the block is
 * released or explicitly checked. It catches out-of-bounds writes where a
 * sanitizer is unavailable or awkward to use.
 *
 * Block metadata is kept out of band, so an underflow into the leading guard
 * cannot prevent the block from being released. Corruption is recorded in
 * `corruptions`; release still succeeds.
 *
 * The decorator is not synchronized. Use one instance per thread, or
 * synchronize externally.
 */

#include <cstdx/allocator.h>
#include <cstdx/api.h>
#include <cstdx/status.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cx_allocator_canary_block;

/**
 * @brief Canary allocator state.
 *
 * Treat the fields as read-only. `inner` and `blocks` are maintained by the
 * implementation.
 */
typedef struct cx_allocator_canary {
    /** Allocator that receives the real requests. */
    const cx_allocator* inner;

    /** Internal list of outstanding blocks. */
    struct cx_allocator_canary_block* blocks;

    /** Live blocks currently owned by the caller. */
    size_t live_blocks;

    /** Number of blocks whose guards were found damaged. */
    size_t corruptions;
} cx_allocator_canary;

/**
 * @brief Initialize a canary allocator.
 *
 * @param canary State to initialize. Must not be nullptr.
 * @param inner Allocator to wrap, or nullptr for `cx_allocator_default`.
 */
[[nodiscard]] CX_API cx_status cx_allocator_canary_init(cx_allocator_canary* canary,
                                                        const cx_allocator* inner);

/**
 * @brief Release the internal tracking records.
 *
 * Outstanding caller blocks are not freed: the caller still owns them.
 */
CX_API void cx_allocator_canary_destroy(cx_allocator_canary* canary);

/**
 * @brief Build an allocator view over @p canary.
 *
 * The view is valid while @p canary lives and is not re-initialized.
 */
[[nodiscard]] CX_API cx_allocator cx_allocator_canary_as_allocator(cx_allocator_canary* canary);

/**
 * @brief Verify the guards around @p block.
 *
 * @return `CX_OK` when the guards are intact, `CX_NOT_FOUND` when @p block was
 *         not produced by this decorator, and `CX_INVALID_STATE` when a guard
 *         byte was changed.
 */
[[nodiscard]] CX_API cx_status cx_allocator_canary_check(const cx_allocator_canary* canary,
                                                         const void* block);

/**
 * @brief Test whether any guard has been damaged.
 */
[[nodiscard]] CX_API bool cx_allocator_canary_has_corruption(const cx_allocator_canary* canary);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_ALLOCATOR_CANARY_H */
