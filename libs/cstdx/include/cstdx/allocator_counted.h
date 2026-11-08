#ifndef CSTDX_ALLOCATOR_COUNTED_H
#define CSTDX_ALLOCATOR_COUNTED_H

/**
 * @file allocator_counted.h
 * @brief Counting/tracking allocator decorator.
 *
 * A counted allocator forwards every request to an inner allocator and records
 * how many blocks are live, how many bytes they own, and the peak live size.
 * It also keeps an out-of-band list of outstanding blocks, which makes it the
 * leak oracle for the rest of the test suite: after a test frees everything,
 * `live_blocks` must be zero.
 *
 * The tracking records are allocated from the inner allocator. They are
 * internal bookkeeping and are not included in the reported byte counters.
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

struct cx_allocator_counted_block;

/**
 * @brief Counting allocator state.
 *
 * Treat the fields as read-only. `inner` and `blocks` are maintained by the
 * implementation.
 */
typedef struct cx_allocator_counted {
    /** Allocator that receives the real requests. */
    const cx_allocator* inner;

    /** Internal list of outstanding blocks. */
    struct cx_allocator_counted_block* blocks;

    /** Successful allocation calls. */
    size_t allocations;

    /** Free calls that released a tracked block. */
    size_t releases;

    /** Live blocks currently owned by the caller. */
    size_t live_blocks;

    /** Sum of the sizes of the live blocks. */
    size_t live_bytes;

    /** High-water mark of `live_bytes`. */
    size_t peak_bytes;

    /** Allocation calls that failed. */
    size_t failed_allocations;
} cx_allocator_counted;

/**
 * @brief Visit one outstanding block.
 *
 * @return true to continue iterating, false to stop.
 */
typedef bool (*cx_allocator_counted_visit_fn)(void* context, const void* block, size_t size,
                                              size_t alignment);

/**
 * @brief Initialize a counting allocator.
 *
 * @param counted State to initialize. Must not be nullptr.
 * @param inner Allocator to wrap, or nullptr for `cx_allocator_default`.
 */
[[nodiscard]] CX_API cx_status cx_allocator_counted_init(cx_allocator_counted* counted,
                                                         const cx_allocator* inner);

/**
 * @brief Release the internal tracking records.
 *
 * Outstanding caller blocks are not freed: the caller still owns them. Free
 * them first, then assert `live_blocks == 0`, then destroy the decorator.
 */
CX_API void cx_allocator_counted_destroy(cx_allocator_counted* counted);

/**
 * @brief Build an allocator view over @p counted.
 *
 * The view is valid while @p counted lives and is not re-initialized.
 */
[[nodiscard]] CX_API cx_allocator cx_allocator_counted_as_allocator(cx_allocator_counted* counted);

/**
 * @brief Test whether any caller block is still outstanding.
 */
[[nodiscard]] CX_API bool cx_allocator_counted_has_leaks(const cx_allocator_counted* counted);

/**
 * @brief Visit every outstanding caller block.
 *
 * @return The number of blocks passed to @p visit. Iteration stops early when
 *         @p visit returns false or when there are no blocks.
 */
[[nodiscard]] CX_API size_t cx_allocator_counted_visit_leaks(const cx_allocator_counted* counted,
                                                             cx_allocator_counted_visit_fn visit,
                                                             void* context);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_ALLOCATOR_COUNTED_H */
