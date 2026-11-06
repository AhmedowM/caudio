#ifndef CSTDX_ARENA_H
#define CSTDX_ARENA_H

/**
 * @file arena.h
 * @brief Arena (bump) allocator with checkpoint/rewind support.
 *
 * An arena serves many small allocations from large chunks and releases them
 * all at once. Individual blocks are never freed; @ref cx_arena_reset and
 * @ref cx_arena_rewind are the only ways to reclaim memory.
 *
 * @section lifetime Lifetime
 *
 * The @ref cx_arena value itself is caller-owned and may live on the stack.
 * It must not be copied or moved after initialization: chunk links and the
 * allocator view keep pointing at the original address.
 *
 * @section invalidation Invalidation
 *
 * @ref cx_arena_reset and @ref cx_arena_rewind invalidate every block
 * allocated after the reset point, including blocks handed out through the
 * @ref cx_allocator view. A checkpoint must not be reused after a reset.
 */

#include <stddef.h>
#include <stdint.h>

#include <cstdx/allocator.h>
#include <cstdx/api.h>
#include <cstdx/status.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cx_arena_chunk cx_arena_chunk;

/**
 * @brief One backing chunk.
 *
 * Implementation detail. Do not access these fields from application code.
 */
struct cx_arena_chunk {
    cx_arena_chunk *previous;
    unsigned char *data;
    size_t capacity;
    size_t used;
    bool owned;
};

/**
 * @brief A position in an arena's history.
 *
 * Treat the fields as opaque. A checkpoint is valid until the arena is reset
 * or destroyed.
 */
typedef struct cx_arena_checkpoint {
    cx_arena_chunk *chunk;
    size_t offset;
    uint64_t epoch;
} cx_arena_checkpoint;

/**
 * @brief Arena state.
 *
 * The fields are an implementation detail; use the functions below. The
 * structure is public so that callers can place it on the stack.
 */
typedef struct cx_arena {
    cx_arena_chunk *current;
    cx_arena_chunk first;
    const cx_allocator *allocator;
    size_t chunk_size;
    size_t used;
    size_t reserved;
    uint64_t epoch;
    bool is_static;
} cx_arena;

/**
 * @brief Initialize an empty arena that grows on demand.
 *
 * No memory is allocated until the first request. @p allocator may be nullptr
 * to use @ref cx_allocator_default; it must outlive the arena otherwise.
 *
 * @return @ref CX_OK, or @ref CX_INVALID_ARGUMENT if @p arena is nullptr or
 *         @p allocator is not usable.
 */
[[nodiscard]] CX_API cx_status cx_arena_init(cx_arena *arena,
                                             const cx_allocator *allocator);

/**
 * @brief Initialize an arena and preallocate a first chunk of @p capacity
 *        bytes.
 *
 * @return @ref CX_OK, @ref CX_INVALID_ARGUMENT, or @ref CX_OUT_OF_MEMORY.
 */
[[nodiscard]] CX_API cx_status cx_arena_init_with_capacity(
    cx_arena *arena, size_t capacity, const cx_allocator *allocator);

/**
 * @brief Initialize an arena backed by a caller-provided buffer.
 *
 * The buffer is never freed by the arena. Allocations that do not fit in
 * @p buffer fail; the arena never grows beyond it. No memory is allocated by
 * this function.
 *
 * The buffer does not need to be aligned; the arena aligns individual blocks
 * internally, wasting at most `alignment - 1` bytes.
 *
 * @return @ref CX_OK, or @ref CX_INVALID_ARGUMENT if @p arena or @p buffer
 *         is nullptr or @p size is zero.
 */
[[nodiscard]] CX_API cx_status cx_arena_init_static(cx_arena *arena,
                                                    void *buffer,
                                                    size_t size);

/**
 * @brief Release every chunk owned by the arena.
 *
 * A static backing buffer is not freed. The arena must not be used after
 * this call except to initialize it again.
 */
CX_API void cx_arena_destroy(cx_arena *arena);

/**
 * @brief Reclaim every allocation and start over.
 *
 * The first backing chunk is retained so that a reset-and-refill cycle does
 * not return to the allocator; additional chunks are released. For a static
 * arena the caller-provided buffer is reused. Every outstanding pointer and
 * checkpoint is invalidated.
 */
CX_API void cx_arena_reset(cx_arena *arena);

/**
 * @brief Allocate @p size bytes aligned to @p alignment.
 *
 * @param alignment A power of two, or 0 for `alignof(max_align_t)`.
 * @return The block, or nullptr if the request does not fit and the arena cannot
 *         grow.
 */
[[nodiscard]] CX_API void *cx_arena_alloc(cx_arena *arena, size_t size,
                                          size_t alignment);

/**
 * @brief Allocate @p size zeroed bytes aligned to @p alignment.
 */
[[nodiscard]] CX_API void *cx_arena_alloc_zeroed(cx_arena *arena, size_t size,
                                                 size_t alignment);

/**
 * @brief Resize a block previously allocated from the arena.
 *
 * Growth happens in place when the block is the most recent allocation in
 * the current chunk and the chunk has room; otherwise a new block is
 * allocated and the old bytes are copied. The old block stays allocated in
 * the arena either way, because arenas do not reclaim individually.
 *
 * @param new_size New size. Zero returns nullptr; nothing is reclaimed.
 * @return The resized block, or nullptr on failure.
 */
[[nodiscard]] CX_API void *cx_arena_realloc(cx_arena *arena, void *ptr,
                                            size_t old_size, size_t new_size,
                                            size_t alignment);

/**
 * @brief Capture the current allocation point.
 *
 * Returns a zeroed checkpoint if @p arena is nullptr.
 */
[[nodiscard]] CX_API cx_arena_checkpoint cx_arena_mark(const cx_arena *arena);

/**
 * @brief Release everything allocated after @p checkpoint.
 *
 * The checkpoint must come from the same arena and the same epoch; a stale
 * checkpoint is ignored in release builds and aborts a debug build.
 */
CX_API void cx_arena_rewind(cx_arena *arena, cx_arena_checkpoint checkpoint);

/**
 * @brief Number of bytes handed out, including alignment padding.
 */
[[nodiscard]] CX_API size_t cx_arena_bytes_used(const cx_arena *arena);

/**
 * @brief Number of bytes currently reserved in backing chunks.
 */
[[nodiscard]] CX_API size_t cx_arena_bytes_reserved(const cx_arena *arena);

/**
 * @brief Build an @ref cx_allocator that allocates from @p arena.
 *
 * The returned value holds a pointer to @p arena and is valid only while the
 * arena lives and is not moved. The allocator's free operation is a no-op,
 * as arenas do not reclaim individual blocks.
 *
 * Returns a zeroed allocator if @p arena is nullptr.
 */
[[nodiscard]] CX_API cx_allocator cx_arena_as_allocator(cx_arena *arena);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_ARENA_H */
