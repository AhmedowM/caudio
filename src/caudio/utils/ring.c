#include <caudio/utils/ring.h>

#include <cstdx/allocator.h>

#include <stdatomic.h>
#include <string.h>

struct ca_ring {
    size_t cap;               /* Capacity in frames. */
    uint32_t channels;        /* Channels per frame. */
    uint8_t* buf;             /* Interleaved storage [cap * channels]. */
    alignas(CA_RING_CACHE_LINE) _Atomic size_t wr; /* Producer index (monotonic). */
    alignas(CA_RING_CACHE_LINE) _Atomic size_t rd; /* Consumer index (monotonic). */
    size_t mask;              /* cap-1 when cap is power-of-two, else 0 (modulo). */
    const cx_allocator* alloc;
};

ca_status ca_ring_create(ca_ring** out,
                         size_t capacity_frames,
                         uint32_t channels,
                         const cx_allocator* alloc) {
    if (!out || capacity_frames == 0 || channels == 0) {
        return CA_INVALID_ARGUMENT;
    }
    const cx_allocator* resolved = alloc ? alloc : cx_allocator_default();

    ca_ring* ring = (ca_ring*)cx_allocator_alloc(resolved, sizeof(ca_ring), alignof(ca_ring));
    if (!ring) {
        return CA_OUT_OF_MEMORY;
    }

    size_t buf_bytes = capacity_frames * channels * sizeof(uint8_t);
    ring->buf = (uint8_t*)cx_allocator_alloc(resolved, buf_bytes, 1);
    if (!ring->buf) {
        cx_allocator_free(resolved, ring, sizeof(ca_ring), alignof(ca_ring));
        return CA_OUT_OF_MEMORY;
    }

    ring->cap = capacity_frames;
    ring->channels = channels;
    ring->buf = ring->buf;
    atomic_store(&ring->wr, 0);
    atomic_store(&ring->rd, 0);
    ring->mask = ((capacity_frames & (capacity_frames - 1)) == 0)
                 ? (capacity_frames - 1) : 0;
    ring->alloc = resolved;

    *out = ring;
    return CA_OK;
}

void ca_ring_destroy(ca_ring* ring) {
    if (!ring) return;
    const cx_allocator* alloc = ring->alloc;
    if (ring->buf) {
        cx_allocator_free(alloc, ring->buf,
                          ring->cap * ring->channels * sizeof(uint8_t), 1);
    }
    cx_allocator_free(alloc, ring, sizeof(ca_ring), alignof(ca_ring));
}

size_t ca_ring_capacity(const ca_ring* ring) {
    return ring ? ring->cap : 0;
}

uint32_t ca_ring_channels(const ca_ring* ring) {
    return ring ? ring->channels : 0;
}

size_t ca_ring_write(ca_ring* ring, const void* data, size_t frames) {
    if (!ring || !data || frames == 0 || ring->cap == 0) return 0;

    size_t wr = atomic_load_explicit(&ring->wr, memory_order_relaxed);
    size_t rd = atomic_load_explicit(&ring->rd, memory_order_acquire);
    size_t used = wr - rd;
    if (used > ring->cap) used = ring->cap;
    size_t free = ring->cap - used;
    if (free == 0) return 0;
    if (frames > free) frames = free;

    size_t ch = ring->channels;
    size_t wr_idx = ring->mask ? (wr & ring->mask) : (wr % ring->cap);
    size_t first = ring->cap - wr_idx;
    if (first > frames) first = frames;

    memcpy(ring->buf + wr_idx * ch, data, first * ch);
    if (frames > first) {
        memcpy(ring->buf, (const uint8_t*)data + first * ch,
               (frames - first) * ch);
    }
    atomic_store_explicit(&ring->wr, wr + frames, memory_order_release);
    return frames;
}

size_t ca_ring_read(ca_ring* ring, void* out, size_t frames) {
    if (!ring || !out || frames == 0 || ring->cap == 0) return 0;

    size_t rd = atomic_load_explicit(&ring->rd, memory_order_relaxed);
    size_t wr = atomic_load_explicit(&ring->wr, memory_order_acquire);
    size_t avail = wr - rd;
    if (avail > ring->cap) avail = ring->cap;
    if (avail == 0) return 0;
    if (frames > avail) frames = avail;

    size_t ch = ring->channels;
    size_t rd_idx = ring->mask ? (rd & ring->mask) : (rd % ring->cap);
    size_t first = ring->cap - rd_idx;
    if (first > frames) first = frames;

    memcpy(out, ring->buf + rd_idx * ch, first * ch);
    if (frames > first) {
        memcpy((uint8_t*)out + first * ch, ring->buf,
               (frames - first) * ch);
    }
    atomic_store_explicit(&ring->rd, rd + frames, memory_order_release);
    return frames;
}

size_t ca_ring_available_read(const ca_ring* ring) {
    if (!ring) return 0;
    size_t rd = atomic_load_explicit(&ring->rd, memory_order_relaxed);
    size_t wr = atomic_load_explicit(&ring->wr, memory_order_acquire);
    size_t avail = wr - rd;
    if (avail > ring->cap) avail = ring->cap;
    return avail;
}

size_t ca_ring_available_write(const ca_ring* ring) {
    if (!ring) return 0;
    size_t wr = atomic_load_explicit(&ring->wr, memory_order_relaxed);
    size_t rd = atomic_load_explicit(&ring->rd, memory_order_acquire);
    size_t used = wr - rd;
    if (used > ring->cap) used = ring->cap;
    return ring->cap - used;
}

void ca_ring_reset(ca_ring* ring) {
    if (!ring) return;
    atomic_store_explicit(&ring->rd, 0, memory_order_release);
    atomic_store_explicit(&ring->wr, 0, memory_order_release);
}
