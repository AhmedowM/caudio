#ifndef CSTDX_POOL_H
#define CSTDX_POOL_H

/**
 * @file pool.h
 * @brief Fixed-size block pool with a threaded free list.
 *
 * A pool serves allocations of one size only. That single-size restriction
 * is what makes it cheap: no headers, no size classes, and O(1) allocation
 * and release. It is a concrete node allocator, not a @ref cx_allocator,
 * because a generic allocator must serve arbitrary sizes.
 *
 * @section lifetime Lifetime
 *
 * The @ref cx_pool value is caller-owned and may live on the stack. It must
 * not be copied or moved after initialization.
 *
 * @section threading Thread safety
 *
 * Pools are not synchronized. Protect them externally if shared.
 */

#include <stddef.h>

#include <cstdx/allocator.h>
#include <cstdx/api.h>
#include <cstdx/status.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cx_pool_chunk cx_pool_chunk;

/**
 * @brief One backing chunk of slots.
 *
 * Implementation detail. Do not access these fields from application code.
 */
struct cx_pool_chunk {
    cx_pool_chunk *previous;
    unsigned char *base;
    size_t slots;
    bool owned;
};

/**
 * @brief Pool state.
 *
 * The fields are an implementation detail; use the functions below. The
 * structure is public so that callers can place it on the stack.
 */
typedef struct cx_pool {
    cx_pool_chunk *current;
    cx_pool_chunk first;
    void *free_list;
    const cx_allocator *allocator;
    size_t slot_size;
    size_t slot_alignment;
    size_t chunk_slots;
    size_t live;
    size_t capacity;
    bool is_static;
} cx_pool;

/**
 * @brief Initialize a dynamic pool.
 *
 * Slots are allocated in chunks of @p chunk_slots slots, grown on demand.
 * @p slot_size is rounded up to a multiple of @p slot_alignment and to at
 * least `sizeof(void *)` (the free list is threaded through the slots).
 *
 * @param slot_size Usable bytes per slot; must be nonzero.
 * @param slot_alignment Power of two, or 0 for `alignof(max_align_t)`.
 * @param chunk_slots Slots per chunk; 0 selects an implementation default.
 * @param allocator Backing allocator, or nullptr for the default allocator.
 * @return @ref CX_OK, @ref CX_INVALID_ARGUMENT, or @ref CX_OVERFLOW.
 */
[[nodiscard]] CX_API cx_status cx_pool_init(cx_pool *pool, size_t slot_size,
                                            size_t slot_alignment,
                                            size_t chunk_slots,
                                            const cx_allocator *allocator);

/**
 * @brief Initialize a pool backed by a caller-provided buffer.
 *
 * The buffer is split into as many slots as fit. The pool never allocates
 * and never grows; allocation fails when the buffer is exhausted. The buffer
 * is aligned internally by skipping `slot_alignment - 1` bytes at most.
 *
 * @return @ref CX_OK, @ref CX_INVALID_ARGUMENT, or @ref CX_OVERFLOW.
 */
[[nodiscard]] CX_API cx_status cx_pool_init_static(cx_pool *pool,
                                                   void *buffer,
                                                   size_t buffer_size,
                                                   size_t slot_size,
                                                   size_t slot_alignment);

/**
 * @brief Release every backing chunk owned by the pool.
 *
 * A static backing buffer is not freed. The pool must not be used after this
 * call except to initialize it again.
 */
CX_API void cx_pool_destroy(cx_pool *pool);

/**
 * @brief Release every slot and start over.
 *
 * The first backing chunk is retained for reuse. Every outstanding slot
 * pointer is invalidated.
 */
CX_API void cx_pool_reset(cx_pool *pool);

/**
 * @brief Take one free slot.
 *
 * @return The slot, or nullptr if the pool is exhausted and cannot grow.
 */
[[nodiscard]] CX_API void *cx_pool_alloc(cx_pool *pool);

/**
 * @brief Return a slot to the pool.
 *
 * @p slot must have been produced by this pool and must currently be
 * allocated. @p slot nullptr is a no-op.
 */
CX_API void cx_pool_free(cx_pool *pool, void *slot);

/**
 * @brief Number of slots currently handed out.
 */
[[nodiscard]] CX_API size_t cx_pool_live(const cx_pool *pool);

/**
 * @brief Number of slots across all backing chunks.
 */
[[nodiscard]] CX_API size_t cx_pool_capacity(const cx_pool *pool);

/**
 * @brief Rounded slot size actually used by the pool.
 */
[[nodiscard]] CX_API size_t cx_pool_slot_size(const cx_pool *pool);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_POOL_H */
