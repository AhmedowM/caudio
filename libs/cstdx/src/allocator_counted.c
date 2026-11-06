#include <cstdx/allocator_counted.h>

#include <stdint.h>
#include <string.h>

#include <cstdx/checked.h>

struct cx_allocator_counted_block {
    void *block;
    size_t size;
    size_t alignment;
    struct cx_allocator_counted_block *next;
};

static const cx_allocator *resolve_inner(const cx_allocator *inner) {
    return inner != nullptr ? inner : cx_allocator_default();
}

static void *counted_alloc(void *state, size_t size, size_t alignment) {
    auto counted = (cx_allocator_counted *)state;
    auto inner = counted->inner;
    auto block = cx_allocator_alloc(inner, size, alignment);

    if (block == nullptr) {
        counted->failed_allocations++;
        return nullptr;
    }

    struct cx_allocator_counted_block *record =
        cx_allocator_alloc(inner, sizeof(*record),
                           alignof(struct cx_allocator_counted_block));

    if (record == nullptr) {
        cx_allocator_free(inner, block, size, alignment);
        counted->failed_allocations++;
        return nullptr;
    }

    record->block = block;
    record->size = size;
    record->alignment = alignment;
    record->next = counted->blocks;
    counted->blocks = record;

    counted->allocations++;
    counted->live_blocks++;

    size_t live;

    if (cx_add_overflow(counted->live_bytes, size, &live)) {
        counted->live_bytes = SIZE_MAX;
    } else {
        counted->live_bytes = live;
    }

    if (counted->live_bytes > counted->peak_bytes) {
        counted->peak_bytes = counted->live_bytes;
    }

    return block;
}

static struct cx_allocator_counted_block **find_record(
    cx_allocator_counted *counted, const void *block) {
    auto link = &counted->blocks;

    while (*link != nullptr) {
        if ((*link)->block == block) {
            return link;
        }

        link = &(*link)->next;
    }

    return nullptr;
}

static void counted_free(void *state, void *block, size_t size,
                          size_t alignment) {
    auto counted = (cx_allocator_counted *)state;

    if (block == nullptr) {
        return;
    }

    auto link = find_record(counted, block);

    if (link != nullptr) {
        auto record = *link;
        *link = record->next;

        counted->releases++;
        counted->live_blocks--;

        auto released = record->size < counted->live_bytes
                            ? record->size
                            : counted->live_bytes;

        counted->live_bytes -= released;
        cx_allocator_free(counted->inner, record, sizeof(*record),
                          alignof(struct cx_allocator_counted_block));
    }

    cx_allocator_free(counted->inner, block, size, alignment);
}

cx_status cx_allocator_counted_init(cx_allocator_counted *counted,
                                    const cx_allocator *inner) {
    if (counted == nullptr || !cx_allocator_is_valid(inner)) {
        return CX_INVALID_ARGUMENT;
    }

    memset(counted, 0, sizeof(*counted));
    counted->inner = resolve_inner(inner);
    return CX_OK;
}

void cx_allocator_counted_destroy(cx_allocator_counted *counted) {
    if (counted == nullptr || counted->inner == nullptr) {
        return;
    }

    auto record = counted->blocks;

    while (record != nullptr) {
        auto next = record->next;
        cx_allocator_free(counted->inner, record, sizeof(*record),
                          alignof(struct cx_allocator_counted_block));
        record = next;
    }

    memset(counted, 0, sizeof(*counted));
}

cx_allocator cx_allocator_counted_as_allocator(cx_allocator_counted *counted) {
    cx_allocator view = {};

    if (counted != nullptr) {
        view.state = counted;
        view.alloc = counted_alloc;
        view.realloc = nullptr;
        view.free = counted_free;
    }

    return view;
}

bool cx_allocator_counted_has_leaks(const cx_allocator_counted *counted) {
    return counted != nullptr && counted->live_blocks != 0;
}

size_t cx_allocator_counted_visit_leaks(
    const cx_allocator_counted *counted, cx_allocator_counted_visit_fn visit,
    void *context) {
    if (counted == nullptr || visit == nullptr) {
        return 0;
    }

    size_t visited = 0;

    for (auto record = counted->blocks; record != nullptr;
         record = record->next) {
        visited++;

        if (!visit(context, record->block, record->size, record->alignment)) {
            break;
        }
    }

    return visited;
}
