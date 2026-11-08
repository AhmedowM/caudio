#ifndef CSTDX_ALLOCATOR_H
#define CSTDX_ALLOCATOR_H

/**
 * @file allocator.h
 * @brief The @ref cx_allocator contract and the default CRT allocator.
 *
 * An allocator is a plain vtable of three function pointers plus an opaque
 * state pointer. It is passed by pointer, never by value, and carries no
 * ownership of the state: the state is whatever the implementation created.
 *
 * @section contract Allocation contract
 *
 * - @p alignment must be a nonzero power of two. The value @c 0 is a
 *   shorthand for `alignof(max_align_t)`. A non-power-of-two alignment is
 *   invalid and allocation returns nullptr.
 * - @p size may be any value; a request for @c 0 bytes returns nullptr and is
 *   not an error.
 * - Allocation failure is reported by returning nullptr; there is no other
 *   failure mode.
 * - @p free(nullptr) is a no-op.
 * - The @p size and @p alignment passed to free must match the values used
 *   to allocate the block. Implementations that do not need them ignore them.
 * - Reallocation of a nullptr pointer behaves like allocation. Reallocation to
 *   @c 0 frees the block and returns nullptr.
 * - A failed reallocation returns nullptr and leaves the original block valid
 *   and unchanged.
 * - Passing a pointer that did not come from the same allocator is undefined
 *   behavior.
 *
 * @section threading Thread safety
 *
 * Allocators are not synchronized unless documented. @ref
 * cx_allocator_default is thread-safe because it forwards to the C runtime.
 *
 * @section nullallocator The nullptr allocator
 *
 * Every cstdx API that takes @c const @ref cx_allocator* accepts nullptr to mean
 * @ref cx_allocator_default. This removes the need for a global default at
 * call sites.
 */

#include <cstdx/api.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cx_allocator cx_allocator;

/**
 * @brief Allocate @p size bytes aligned to @p alignment.
 * @return The block, or nullptr on failure.
 */
typedef void* (*cx_allocator_alloc_fn)(void* state, size_t size, size_t alignment);

/**
 * @brief Resize a block previously produced by @p state.
 *
 * May be nullptr, in which case callers fall back to allocate/copy/free.
 *
 * @return The resized block, or nullptr on failure (original block intact).
 */
typedef void* (*cx_allocator_realloc_fn)(void* state, void* ptr, size_t old_size, size_t new_size,
                                         size_t alignment);

/**
 * @brief Release a block previously produced by @p state.
 *
 * @p size and @p alignment are the ones used for the allocation.
 */
typedef void (*cx_allocator_free_fn)(void* state, void* ptr, size_t size, size_t alignment);

/**
 * @brief A source of memory.
 *
 * Treat the fields as read-only after construction.
 */
struct cx_allocator {
    /** Opaque implementation state; passed back to every callback. */
    void* state;

    /** Required. */
    cx_allocator_alloc_fn alloc;

    /** Optional; nullptr selects the allocate/copy/free fallback. */
    cx_allocator_realloc_fn realloc;

    /** Required. */
    cx_allocator_free_fn free;
};

/**
 * @brief Return the process-wide default allocator.
 *
 * The allocator forwards to the C runtime (`aligned_alloc`/`posix_memalign`
 * or `_aligned_malloc` on Windows). It is thread-safe.
 */
[[nodiscard]] CX_API const cx_allocator* cx_allocator_default(void);

/**
 * @brief Test whether an allocator pointer can be used for allocation.
 *
 * nullptr denotes the default allocator and is considered valid.
 */
[[nodiscard]] CX_API bool cx_allocator_is_valid(const cx_allocator* allocator);

/**
 * @brief Allocate @p size bytes with @p alignment.
 *
 * @param allocator Allocator, or nullptr for the default allocator.
 * @return The block, or nullptr if @p size is zero, @p alignment is invalid, or
 *         the allocator is out of memory.
 */
[[nodiscard]] CX_API void* cx_allocator_alloc(const cx_allocator* allocator, size_t size,
                                              size_t alignment);

/**
 * @brief Allocate @p size zeroed bytes with @p alignment.
 */
[[nodiscard]] CX_API void* cx_allocator_alloc_zeroed(const cx_allocator* allocator, size_t size,
                                                     size_t alignment);

/**
 * @brief Resize a block, falling back to allocate/copy/free when needed.
 *
 * @param allocator The allocator that produced @p ptr, or nullptr for default.
 * @param ptr Block to resize, or nullptr to allocate a new block.
 * @param old_size Size of @p ptr; ignored when @p ptr is nullptr.
 * @param new_size Requested size; zero frees @p ptr and returns nullptr.
 * @return The resized block, or nullptr on failure. On failure @p ptr is still
 *         valid.
 */
[[nodiscard]] CX_API void* cx_allocator_realloc(const cx_allocator* allocator, void* ptr,
                                                size_t old_size, size_t new_size, size_t alignment);

/**
 * @brief Release a block.
 *
 * @param allocator The allocator that produced @p ptr.
 * @param ptr Block to release, or nullptr.
 * @param size Size passed at allocation time.
 * @param alignment Alignment passed at allocation time.
 */
CX_API void cx_allocator_free(const cx_allocator* allocator, void* ptr, size_t size,
                              size_t alignment);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_ALLOCATOR_H */
