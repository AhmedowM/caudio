#include <cstdx/allocator_canary.h>
#include <cstdx/checked.h>
#include <stdint.h>
#include <string.h>

static constexpr size_t cx_canary_size = 16;
static constexpr unsigned char cx_canary_seed = 0xC5;

struct cx_allocator_canary_block {
    void* base;
    void* block;
    size_t total;
    size_t size;
    size_t alignment;
    struct cx_allocator_canary_block* next;
};

static const cx_allocator* resolve_inner(const cx_allocator* inner) {
    return inner != nullptr ? inner : cx_allocator_default();
}

static unsigned char canary_byte(size_t index) {
    return (unsigned char)(cx_canary_seed + (unsigned char)(index * 31u));
}

static void canary_write(unsigned char* guard) {
    for (size_t i = 0; i < cx_canary_size; ++i) {
        guard[i] = canary_byte(i);
    }
}

static bool canary_intact(const unsigned char* guard) {
    for (size_t i = 0; i < cx_canary_size; ++i) {
        if (guard[i] != canary_byte(i)) {
            return false;
        }
    }

    return true;
}

static void* canary_alloc(void* state, size_t size, size_t alignment) {
    auto canary = (cx_allocator_canary*)state;

    if (!cx_is_power_of_two(alignment)) {
        return nullptr;
    }

    size_t overhead;

    if (cx_add_overflow(cx_canary_size * 2, alignment - 1, &overhead) ||
        cx_add_overflow(overhead, size, &overhead)) {
        return nullptr;
    }

    auto inner = canary->inner;
    auto base = cx_allocator_alloc(inner, overhead, alignment);

    if (base == nullptr) {
        return nullptr;
    }

    auto after_front = (uintptr_t)base + (uintptr_t)cx_canary_size;
    auto aligned = (after_front + (uintptr_t)(alignment - 1)) & ~((uintptr_t)alignment - 1);
    auto block = (unsigned char*)aligned;
    auto front = block - cx_canary_size;
    auto back = block + size;

    canary_write(front);
    canary_write(back);

    struct cx_allocator_canary_block* record =
        cx_allocator_alloc(inner, sizeof(*record), alignof(struct cx_allocator_canary_block));

    if (record == nullptr) {
        cx_allocator_free(inner, base, overhead, alignment);
        return nullptr;
    }

    record->base = base;
    record->block = block;
    record->total = overhead;
    record->size = size;
    record->alignment = alignment;
    record->next = canary->blocks;
    canary->blocks = record;
    canary->live_blocks++;
    return block;
}

static struct cx_allocator_canary_block** find_record(cx_allocator_canary* canary,
                                                      const void* block) {
    auto link = &canary->blocks;

    while (*link != nullptr) {
        if ((*link)->block == block) {
            return link;
        }

        link = &(*link)->next;
    }

    return nullptr;
}

static void canary_free(void* state, void* block, [[maybe_unused]] size_t size,
                        [[maybe_unused]] size_t alignment) {
    auto canary = (cx_allocator_canary*)state;

    if (block == nullptr) {
        return;
    }

    auto link = find_record(canary, block);

    if (link == nullptr) {
        canary->corruptions++;
        return;
    }

    auto record = *link;
    *link = record->next;

    auto bytes = (const unsigned char*)block;

    if (!canary_intact(bytes - cx_canary_size) || !canary_intact(bytes + record->size)) {
        canary->corruptions++;
    }

    canary->live_blocks--;
    cx_allocator_free(canary->inner, record->base, record->total, record->alignment);
    cx_allocator_free(canary->inner, record, sizeof(*record),
                      alignof(struct cx_allocator_canary_block));
}

cx_status cx_allocator_canary_init(cx_allocator_canary* canary, const cx_allocator* inner) {
    if (canary == nullptr || !cx_allocator_is_valid(inner)) {
        return CX_INVALID_ARGUMENT;
    }

    memset(canary, 0, sizeof(*canary));
    canary->inner = resolve_inner(inner);
    return CX_OK;
}

void cx_allocator_canary_destroy(cx_allocator_canary* canary) {
    if (canary == nullptr || canary->inner == nullptr) {
        return;
    }

    auto record = canary->blocks;

    while (record != nullptr) {
        auto next = record->next;
        cx_allocator_free(canary->inner, record, sizeof(*record),
                          alignof(struct cx_allocator_canary_block));
        record = next;
    }

    memset(canary, 0, sizeof(*canary));
}

cx_allocator cx_allocator_canary_as_allocator(cx_allocator_canary* canary) {
    cx_allocator view = {};

    if (canary != nullptr) {
        view.state = canary;
        view.alloc = canary_alloc;
        view.realloc = nullptr;
        view.free = canary_free;
    }

    return view;
}

cx_status cx_allocator_canary_check(const cx_allocator_canary* canary, const void* block) {
    if (canary == nullptr || block == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    for (auto record = canary->blocks; record != nullptr; record = record->next) {
        if (record->block == block) {
            auto bytes = (const unsigned char*)block;

            if (!canary_intact(bytes - cx_canary_size) || !canary_intact(bytes + record->size)) {
                return CX_INVALID_STATE;
            }

            return CX_OK;
        }
    }

    return CX_NOT_FOUND;
}

bool cx_allocator_canary_has_corruption(const cx_allocator_canary* canary) {
    return canary != nullptr && canary->corruptions != 0;
}
