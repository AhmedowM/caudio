#include <assert.h>
#include <cstdx/arena.h>
#include <cstdx/checked.h>
#include <string.h>

static constexpr size_t arena_default_chunk_size = 4096;

static size_t arena_normalize_alignment(size_t alignment) {
    return alignment == 0 ? alignof(max_align_t) : alignment;
}

/* ------------------------------------------------------------------------- */
/* Backing chunks                                                            */
/* ------------------------------------------------------------------------- */

/**
 * Ensure the arena can serve an allocation of @p required bytes with
 * @p alignment. Adds @p alignment bytes of slack so that the next chunk can
 * always produce an aligned address.
 */
static bool arena_grow(cx_arena* arena, size_t required, size_t alignment) {
    if (arena->is_static) {
        return false;
    }

    size_t minimum;

    if (cx_add_overflow(required, alignment, &minimum)) {
        return false;
    }

    auto capacity = arena->chunk_size;

    if (capacity < minimum) {
        capacity = minimum;
    }

    auto allocator = arena->allocator;
    auto data = cx_allocator_alloc(allocator, capacity, alignof(max_align_t));

    if (data == nullptr) {
        return false;
    }

    cx_arena_chunk* chunk;

    if (arena->current == &arena->first && arena->first.data == nullptr) {
        chunk = &arena->first;
        chunk->previous = nullptr;
    } else {
        chunk = cx_allocator_alloc(allocator, sizeof(*chunk), alignof(cx_arena_chunk));

        if (chunk == nullptr) {
            cx_allocator_free(allocator, data, capacity, alignof(max_align_t));
            return false;
        }

        chunk->previous = arena->current;
        arena->current = chunk;
    }

    chunk->data = data;
    chunk->capacity = capacity;
    chunk->used = 0;
    chunk->owned = true;

    arena->reserved += capacity;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------- */

cx_status cx_arena_init(cx_arena* arena, const cx_allocator* allocator) {
    if (arena == nullptr || !cx_allocator_is_valid(allocator)) {
        return CX_INVALID_ARGUMENT;
    }

    memset(arena, 0, sizeof(*arena));

    arena->current = &arena->first;
    arena->allocator = allocator != nullptr ? allocator : cx_allocator_default();
    arena->chunk_size = arena_default_chunk_size;
    arena->epoch = 1;
    return CX_OK;
}

cx_status cx_arena_init_with_capacity(cx_arena* arena, size_t capacity,
                                      const cx_allocator* allocator) {
    auto status = cx_arena_init(arena, allocator);

    if (status != CX_OK || capacity == 0) {
        return status;
    }

    arena->chunk_size = capacity;

    if (!arena_grow(arena, 1, alignof(max_align_t))) {
        cx_arena_destroy(arena);
        return CX_OUT_OF_MEMORY;
    }

    return CX_OK;
}

cx_status cx_arena_init_static(cx_arena* arena, void* buffer, size_t size) {
    if (arena == nullptr || buffer == nullptr || size == 0) {
        return CX_INVALID_ARGUMENT;
    }

    memset(arena, 0, sizeof(*arena));

    arena->first.data = (unsigned char*)buffer;
    arena->first.capacity = size;
    arena->current = &arena->first;
    arena->allocator = cx_allocator_default();
    arena->reserved = size;
    arena->epoch = 1;
    arena->is_static = true;
    return CX_OK;
}

void cx_arena_destroy(cx_arena* arena) {
    if (arena == nullptr || arena->current == nullptr) {
        return;
    }

    if (!arena->is_static) {
        while (arena->current != &arena->first) {
            auto chunk = arena->current;
            arena->current = chunk->previous;
            cx_allocator_free(arena->allocator, chunk->data, chunk->capacity, alignof(max_align_t));
            cx_allocator_free(arena->allocator, chunk, sizeof(*chunk), alignof(cx_arena_chunk));
        }

        if (arena->first.owned && arena->first.data != nullptr) {
            cx_allocator_free(arena->allocator, arena->first.data, arena->first.capacity,
                              alignof(max_align_t));
        }
    }

    memset(arena, 0, sizeof(*arena));
}

void cx_arena_reset(cx_arena* arena) {
    if (arena == nullptr || arena->current == nullptr) {
        return;
    }

    if (arena->is_static) {
        arena->first.used = 0;
        arena->current = &arena->first;
        arena->used = 0;
        arena->epoch++;
        return;
    }

    while (arena->current != &arena->first) {
        auto chunk = arena->current;
        arena->current = chunk->previous;
        cx_allocator_free(arena->allocator, chunk->data, chunk->capacity, alignof(max_align_t));
        cx_allocator_free(arena->allocator, chunk, sizeof(*chunk), alignof(cx_arena_chunk));
    }

    arena->first.used = 0;
    arena->used = 0;
    /* The first buffer is kept so a reset/refill cycle does not need the
     * allocator again. */
    arena->reserved = arena->first.capacity;
    arena->epoch++;
}

/* ------------------------------------------------------------------------- */
/* Allocation                                                                */
/* ------------------------------------------------------------------------- */

void* cx_arena_alloc(cx_arena* arena, size_t size, size_t alignment) {
    if (arena == nullptr || arena->current == nullptr || size == 0) {
        return nullptr;
    }

    alignment = arena_normalize_alignment(alignment);

    if (!cx_is_power_of_two(alignment)) {
        return nullptr;
    }

    for (;;) {
        auto chunk = arena->current;

        if (chunk->data != nullptr) {
            auto base = (uintptr_t)chunk->data;
            auto address = base + (uintptr_t)chunk->used;
            auto aligned = (address + (uintptr_t)(alignment - 1)) & ~((uintptr_t)alignment - 1);
            auto offset = (size_t)(aligned - base);
            size_t end;

            if (!cx_add_overflow(offset, size, &end) && end <= chunk->capacity) {
                arena->used += end - chunk->used;
                chunk->used = end;
                return (void*)aligned;
            }
        }

        if (!arena_grow(arena, size, alignment)) {
            return nullptr;
        }
    }
}

void* cx_arena_alloc_zeroed(cx_arena* arena, size_t size, size_t alignment) {
    auto block = cx_arena_alloc(arena, size, alignment);

    if (block != nullptr) {
        memset(block, 0, size);
    }

    return block;
}

void* cx_arena_realloc(cx_arena* arena, void* ptr, size_t old_size, size_t new_size,
                       size_t alignment) {
    if (arena == nullptr || arena->current == nullptr) {
        return nullptr;
    }

    if (ptr == nullptr) {
        return cx_arena_alloc(arena, new_size, alignment);
    }

    if (new_size == 0) {
        /* Arenas do not reclaim individual blocks. */
        return nullptr;
    }

    alignment = arena_normalize_alignment(alignment);

    if (!cx_is_power_of_two(alignment)) {
        return nullptr;
    }

    auto chunk = arena->current;

    if (chunk->data != nullptr) {
        auto base = (uintptr_t)chunk->data;
        auto end = base + (uintptr_t)chunk->used;
        auto address = (uintptr_t)ptr;

        if (address >= base && address < end && address + (uintptr_t)old_size == end) {
            if (new_size <= old_size) {
                auto shrink = old_size - new_size;

                chunk->used -= shrink;
                arena->used -= shrink;
                return ptr;
            }

            auto growth = new_size - old_size;

            if (growth <= chunk->capacity - chunk->used) {
                chunk->used += growth;
                arena->used += growth;
                return ptr;
            }
        }
    }

    auto resized = cx_arena_alloc(arena, new_size, alignment);

    if (resized == nullptr) {
        return nullptr;
    }

    memmove(resized, ptr, old_size < new_size ? old_size : new_size);
    return resized;
}

/* ------------------------------------------------------------------------- */
/* Checkpoints                                                               */
/* ------------------------------------------------------------------------- */

cx_arena_checkpoint cx_arena_mark(const cx_arena* arena) {
    cx_arena_checkpoint checkpoint = {};

    if (arena == nullptr || arena->current == nullptr) {
        return checkpoint;
    }

    checkpoint.chunk = arena->current;
    checkpoint.offset = arena->current->used;
    checkpoint.epoch = arena->epoch;
    return checkpoint;
}

void cx_arena_rewind(cx_arena* arena, cx_arena_checkpoint checkpoint) {
    if (arena == nullptr || arena->current == nullptr) {
        return;
    }

    if (checkpoint.chunk == nullptr || checkpoint.epoch != arena->epoch) {
        assert(0 && "invalid arena checkpoint");
        return;
    }

    auto cursor = arena->current;

    while (cursor != nullptr && cursor != checkpoint.chunk) {
        cursor = cursor->previous;
    }

    if (cursor == nullptr) {
        assert(0 && "checkpoint does not belong to this arena");
        return;
    }

    if (checkpoint.offset > checkpoint.chunk->used) {
        assert(0 && "invalid arena checkpoint offset");
        return;
    }

    while (arena->current != checkpoint.chunk) {
        auto chunk = arena->current;
        arena->current = chunk->previous;
        arena->used -= chunk->used;
        arena->reserved -= chunk->capacity;
        cx_allocator_free(arena->allocator, chunk->data, chunk->capacity, alignof(max_align_t));
        cx_allocator_free(arena->allocator, chunk, sizeof(*chunk), alignof(cx_arena_chunk));
    }

    arena->used -= arena->current->used - checkpoint.offset;
    arena->current->used = checkpoint.offset;
}

/* ------------------------------------------------------------------------- */
/* Introspection                                                             */
/* ------------------------------------------------------------------------- */

size_t cx_arena_bytes_used(const cx_arena* arena) { return arena != nullptr ? arena->used : 0; }

size_t cx_arena_bytes_reserved(const cx_arena* arena) {
    return arena != nullptr ? arena->reserved : 0;
}

/* ------------------------------------------------------------------------- */
/* Allocator view                                                            */
/* ------------------------------------------------------------------------- */

static void* arena_view_alloc(void* state, size_t size, size_t alignment) {
    return cx_arena_alloc((cx_arena*)state, size, alignment);
}

static void* arena_view_realloc(void* state, void* ptr, size_t old_size, size_t new_size,
                                size_t alignment) {
    return cx_arena_realloc((cx_arena*)state, ptr, old_size, new_size, alignment);
}

static void arena_view_free(void*, void*, size_t, size_t) {}

cx_allocator cx_arena_as_allocator(cx_arena* arena) {
    cx_allocator view = {};

    if (arena != nullptr) {
        view.state = arena;
        view.alloc = arena_view_alloc;
        view.realloc = arena_view_realloc;
        view.free = arena_view_free;
    }

    return view;
}
