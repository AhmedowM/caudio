#include <assert.h>
#include <cstdx/checked.h>
#include <cstdx/pool.h>
#include <stdint.h>
#include <string.h>

static constexpr size_t pool_default_chunk_slots = 64;

static size_t pool_normalize_alignment(size_t alignment) {
    if (alignment == 0) {
        alignment = alignof(max_align_t);
    }

    if (!cx_is_power_of_two(alignment)) {
        return 0;
    }

    if (alignment < alignof(void*)) {
        alignment = alignof(void*);
    }

    return alignment;
}

static void pool_thread_chunk(cx_pool* pool, cx_pool_chunk* chunk) {
    for (auto i = chunk->slots; i-- > 0;) {
        auto slot = chunk->base + i * pool->slot_size;

        *(void**)slot = pool->free_list;
        pool->free_list = slot;
    }
}

static bool pool_grow(cx_pool* pool) {
    if (pool->is_static) {
        return false;
    }

    size_t bytes;

    if (cx_mul_overflow(pool->chunk_slots, pool->slot_size, &bytes)) {
        return false;
    }

    auto allocator = pool->allocator;
    auto base = cx_allocator_alloc(allocator, bytes, pool->slot_alignment);

    if (base == nullptr) {
        return false;
    }

    cx_pool_chunk* chunk;

    if (pool->current == &pool->first && pool->first.base == nullptr) {
        chunk = &pool->first;
        chunk->previous = nullptr;
    } else {
        chunk = cx_allocator_alloc(allocator, sizeof(*chunk), alignof(cx_pool_chunk));

        if (chunk == nullptr) {
            cx_allocator_free(allocator, base, bytes, pool->slot_alignment);
            return false;
        }

        chunk->previous = pool->current;
        pool->current = chunk;
    }

    chunk->base = base;
    chunk->slots = pool->chunk_slots;
    chunk->owned = true;

    pool->capacity += chunk->slots;
    pool_thread_chunk(pool, chunk);
    return true;
}

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------- */

static cx_status pool_configure(cx_pool* pool, size_t slot_size, size_t slot_alignment,
                                size_t chunk_slots, const cx_allocator* allocator) {
    if (pool == nullptr || slot_size == 0 || !cx_allocator_is_valid(allocator)) {
        return CX_INVALID_ARGUMENT;
    }

    slot_alignment = pool_normalize_alignment(slot_alignment);

    if (slot_alignment == 0) {
        return CX_INVALID_ARGUMENT;
    }

    size_t rounded;

    if (!cx_align_up(slot_size, slot_alignment, &rounded)) {
        return CX_OVERFLOW;
    }

    if (rounded < sizeof(void*)) {
        if (!cx_align_up(sizeof(void*), slot_alignment, &rounded)) {
            return CX_OVERFLOW;
        }
    }

    memset(pool, 0, sizeof(*pool));

    pool->slot_size = rounded;
    pool->slot_alignment = slot_alignment;
    pool->chunk_slots = chunk_slots != 0 ? chunk_slots : pool_default_chunk_slots;
    pool->allocator = allocator != nullptr ? allocator : cx_allocator_default();
    pool->current = &pool->first;
    return CX_OK;
}

cx_status cx_pool_init(cx_pool* pool, size_t slot_size, size_t slot_alignment, size_t chunk_slots,
                       const cx_allocator* allocator) {
    return pool_configure(pool, slot_size, slot_alignment, chunk_slots, allocator);
}

cx_status cx_pool_init_static(cx_pool* pool, void* buffer, size_t buffer_size, size_t slot_size,
                              size_t slot_alignment) {
    auto status = pool_configure(pool, slot_size, slot_alignment, 0, nullptr);

    if (status != CX_OK) {
        return status;
    }

    if (buffer == nullptr || buffer_size == 0) {
        return CX_INVALID_ARGUMENT;
    }

    auto base = (uintptr_t)buffer;
    auto aligned =
        (base + (uintptr_t)(pool->slot_alignment - 1)) & ~((uintptr_t)pool->slot_alignment - 1);
    auto skip = (size_t)(aligned - base);

    if (skip >= buffer_size) {
        return CX_INVALID_ARGUMENT;
    }

    auto slots = (buffer_size - skip) / pool->slot_size;

    if (slots == 0) {
        return CX_INVALID_ARGUMENT;
    }

    pool->first.base = (unsigned char*)aligned;
    pool->first.slots = slots;
    pool->current = &pool->first;
    pool->capacity = slots;
    pool->is_static = true;
    pool_thread_chunk(pool, &pool->first);
    return CX_OK;
}

void cx_pool_destroy(cx_pool* pool) {
    if (pool == nullptr || pool->current == nullptr) {
        return;
    }

    if (!pool->is_static) {
        while (pool->current != &pool->first) {
            auto chunk = pool->current;
            pool->current = chunk->previous;
            cx_allocator_free(pool->allocator, chunk->base, chunk->slots * pool->slot_size,
                              pool->slot_alignment);
            cx_allocator_free(pool->allocator, chunk, sizeof(*chunk), alignof(cx_pool_chunk));
        }

        if (pool->first.owned && pool->first.base != nullptr) {
            cx_allocator_free(pool->allocator, pool->first.base,
                              pool->first.slots * pool->slot_size, pool->slot_alignment);
        }
    }

    memset(pool, 0, sizeof(*pool));
}

void cx_pool_reset(cx_pool* pool) {
    if (pool == nullptr || pool->current == nullptr) {
        return;
    }

    if (pool->is_static) {
        pool->current = &pool->first;
        pool->live = 0;
        pool->free_list = nullptr;
        pool->capacity = pool->first.slots;
        pool_thread_chunk(pool, &pool->first);
        return;
    }

    while (pool->current != &pool->first) {
        auto chunk = pool->current;
        pool->current = chunk->previous;
        cx_allocator_free(pool->allocator, chunk->base, chunk->slots * pool->slot_size,
                          pool->slot_alignment);
        cx_allocator_free(pool->allocator, chunk, sizeof(*chunk), alignof(cx_pool_chunk));
    }

    pool->live = 0;
    pool->free_list = nullptr;

    if (pool->first.base != nullptr) {
        pool->capacity = pool->first.slots;
        pool_thread_chunk(pool, &pool->first);
    } else {
        pool->first.slots = 0;
        pool->capacity = 0;
    }
}

/* ------------------------------------------------------------------------- */
/* Allocation                                                                */
/* ------------------------------------------------------------------------- */

void* cx_pool_alloc(cx_pool* pool) {
    if (pool == nullptr || pool->current == nullptr) {
        return nullptr;
    }

    if (pool->free_list == nullptr && !pool_grow(pool)) {
        return nullptr;
    }

    auto slot = pool->free_list;

    pool->free_list = *(void**)slot;
    pool->live++;
    return slot;
}

#ifndef NDEBUG

static bool pool_contains(const cx_pool* pool, const void* slot) {
    auto address = (uintptr_t)slot;
    auto chunk = pool->current;

    while (chunk != nullptr) {
        auto base = (uintptr_t)chunk->base;
        auto end = base + (uintptr_t)chunk->slots * pool->slot_size;

        if (address >= base && address < end) {
            return (address - base) % pool->slot_size == 0;
        }

        chunk = chunk->previous;
    }

    return false;
}

#endif /* !NDEBUG */

void cx_pool_free(cx_pool* pool, void* slot) {
    if (pool == nullptr || slot == nullptr) {
        return;
    }

#ifndef NDEBUG
    assert(pool_contains(pool, slot));
#endif

    *(void**)slot = pool->free_list;
    pool->free_list = slot;
    pool->live--;
}

/* ------------------------------------------------------------------------- */
/* Introspection                                                             */
/* ------------------------------------------------------------------------- */

size_t cx_pool_live(const cx_pool* pool) { return pool != nullptr ? pool->live : 0; }

size_t cx_pool_capacity(const cx_pool* pool) { return pool != nullptr ? pool->capacity : 0; }

size_t cx_pool_slot_size(const cx_pool* pool) { return pool != nullptr ? pool->slot_size : 0; }
