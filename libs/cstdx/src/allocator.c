#include <cstdx/allocator.h>
#include <cstdx/checked.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <malloc.h>
#define CX_USE_WIN32_ALIGNED 1
#else
#define CX_USE_WIN32_ALIGNED 0
#endif

static constexpr size_t cx_default_alignment = alignof(max_align_t);
static constexpr size_t cx_minimum_alignment = sizeof(void*);

/* ------------------------------------------------------------------------- */
/* Platform allocation                                                       */
/* ------------------------------------------------------------------------- */

static void* os_alloc(size_t size, size_t alignment) {
#if CX_USE_WIN32_ALIGNED
    return _aligned_malloc(size, alignment);
#else
    size_t rounded;

    if (!cx_align_up(size, alignment, &rounded)) {
        return nullptr;
    }

    return aligned_alloc(alignment, rounded);
#endif
}

static void os_free(void* ptr) {
#if CX_USE_WIN32_ALIGNED
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

static void* os_realloc(void* ptr, [[maybe_unused]] size_t old_size, size_t new_size,
                        size_t alignment) {
#if CX_USE_WIN32_ALIGNED
    return _aligned_realloc(ptr, new_size, alignment);
#else
    auto resized = os_alloc(new_size, alignment);

    if (resized == nullptr) {
        return nullptr;
    }

    memcpy(resized, ptr, old_size < new_size ? old_size : new_size);
    os_free(ptr);
    return resized;
#endif
}

/* ------------------------------------------------------------------------- */
/* Default allocator                                                         */
/* ------------------------------------------------------------------------- */

static void* default_alloc(void*, size_t size, size_t alignment) {
    if (size == 0) {
        return nullptr;
    }

    return os_alloc(size, alignment);
}

static void* default_realloc(void*, void* ptr, size_t old_size, size_t new_size, size_t alignment) {
    return os_realloc(ptr, old_size, new_size, alignment);
}

static void default_free(void*, void* ptr, size_t, size_t) {
    if (ptr != nullptr) {
        os_free(ptr);
    }
}

static const cx_allocator g_default_allocator = {
    .state = nullptr,
    .alloc = default_alloc,
    .realloc = default_realloc,
    .free = default_free,
};

const cx_allocator* cx_allocator_default(void) { return &g_default_allocator; }

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

/**
 * Normalize a caller-supplied alignment.
 *
 * Returns 0 when the alignment is not a power of two.
 */
static size_t normalize_alignment(size_t alignment) {
    if (alignment == 0) {
        alignment = cx_default_alignment;
    }

    if (!cx_is_power_of_two(alignment)) {
        return 0;
    }

    /* Keep every alignment usable by the platform allocator and by code that
     * stores a pointer in the block. Over-alignment is always allowed. */
    if (alignment < cx_minimum_alignment) {
        alignment = cx_minimum_alignment;
    }

    return alignment;
}

static const cx_allocator* resolve(const cx_allocator* allocator) {
    return allocator != nullptr ? allocator : cx_allocator_default();
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

bool cx_allocator_is_valid(const cx_allocator* allocator) {
    if (allocator == nullptr) {
        return true;
    }

    return allocator->alloc != nullptr && allocator->free != nullptr;
}

void* cx_allocator_alloc(const cx_allocator* allocator, size_t size, size_t alignment) {
    auto resolved = resolve(allocator);

    if (size == 0 || resolved->alloc == nullptr) {
        return nullptr;
    }

    auto normalized = normalize_alignment(alignment);

    if (normalized == 0) {
        return nullptr;
    }

    return resolved->alloc(resolved->state, size, normalized);
}

void* cx_allocator_alloc_zeroed(const cx_allocator* allocator, size_t size, size_t alignment) {
    auto block = cx_allocator_alloc(allocator, size, alignment);

    if (block != nullptr) {
        memset(block, 0, size);
    }

    return block;
}

void* cx_allocator_realloc(const cx_allocator* allocator, void* ptr, size_t old_size,
                           size_t new_size, size_t alignment) {
    auto resolved = resolve(allocator);
    auto normalized = normalize_alignment(alignment);

    if (normalized == 0) {
        return nullptr;
    }

    if (ptr == nullptr) {
        return cx_allocator_alloc(resolved, new_size, normalized);
    }

    if (new_size == 0) {
        cx_allocator_free(resolved, ptr, old_size, normalized);
        return nullptr;
    }

    if (resolved->realloc != nullptr) {
        return resolved->realloc(resolved->state, ptr, old_size, new_size, normalized);
    }

    auto resized = cx_allocator_alloc(resolved, new_size, normalized);

    if (resized == nullptr) {
        return nullptr;
    }

    memcpy(resized, ptr, old_size < new_size ? old_size : new_size);
    cx_allocator_free(resolved, ptr, old_size, normalized);
    return resized;
}

void cx_allocator_free(const cx_allocator* allocator, void* ptr, size_t size, size_t alignment) {
    if (ptr == nullptr) {
        return;
    }

    auto resolved = resolve(allocator);

    if (resolved->free == nullptr) {
        return;
    }

    /* free() must not fail. If the caller passed a nonsense alignment, fall
     * back to the default instead of skipping the release. */
    auto normalized = normalize_alignment(alignment);

    if (normalized == 0) {
        normalized = cx_default_alignment;
    }

    resolved->free(resolved->state, ptr, size, normalized);
}
