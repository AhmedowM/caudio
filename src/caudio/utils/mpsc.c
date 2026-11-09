#include <caudio/utils/mpsc.h>

#include <cstdx/allocator.h>

#include <caudio/runtime/cond.h>
#include <caudio/runtime/error.h>
#include <caudio/runtime/mutex.h>

#include <stdatomic.h>
#include <string.h>

struct ca_mpsc {
    size_t cap;
    size_t elem_size;
    uint8_t* buf;      /* Circular storage [cap * elem_size]. */
    _Atomic size_t wr; /* Monotonic write counter. */
    _Atomic size_t rd; /* Monotonic read counter. */
    ca_mutex_t mutex;
    ca_cond_t cond;
    const cx_allocator* alloc;
};

ca_status ca_mpsc_create(ca_mpsc** out,
                         size_t capacity,
                         size_t elem_size,
                         const cx_allocator* alloc) {
    if (!out || capacity == 0 || elem_size == 0) return CA_INVALID_ARGUMENT;
    if (capacity == 0) capacity = 1;

    const cx_allocator* resolved = alloc ? alloc : cx_allocator_default();

    ca_mpsc* q = (ca_mpsc*)cx_allocator_alloc(resolved, sizeof(ca_mpsc), alignof(ca_mpsc));
    if (!q) return CA_OUT_OF_MEMORY;

    q->buf = (uint8_t*)cx_allocator_alloc(resolved, capacity * elem_size, 1);
    if (!q->buf) {
        cx_allocator_free(resolved, q, sizeof(ca_mpsc), alignof(ca_mpsc));
        return CA_OUT_OF_MEMORY;
    }

    ca_error err;
    ca_error_init(&err);

    q->cap = capacity;
    q->elem_size = elem_size;
    atomic_store(&q->wr, 0);
    atomic_store(&q->rd, 0);
    q->alloc = resolved;

    ca_status st = ca_mutex_create(&q->mutex, &err);
    if (st != CA_OK) {
        cx_allocator_free(resolved, q->buf, capacity * elem_size, 1);
        cx_allocator_free(resolved, q, sizeof(ca_mpsc), alignof(ca_mpsc));
        return st;
    }
    st = ca_cond_create(&q->cond, &err);
    if (st != CA_OK) {
        ca_mutex_destroy(q->mutex);
        cx_allocator_free(resolved, q->buf, capacity * elem_size, 1);
        cx_allocator_free(resolved, q, sizeof(ca_mpsc), alignof(ca_mpsc));
        return st;
    }

    *out = q;
    return CA_OK;
}

void ca_mpsc_destroy(ca_mpsc* queue) {
    if (!queue) return;
    const cx_allocator* alloc = queue->alloc;
    ca_error err;
    ca_error_init(&err);
    if (queue->mutex) ca_mutex_destroy(queue->mutex);
    if (queue->cond) ca_cond_destroy(queue->cond);
    if (queue->buf) cx_allocator_free(alloc, queue->buf, queue->cap * queue->elem_size, 1);
    cx_allocator_free(alloc, queue, sizeof(ca_mpsc), alignof(ca_mpsc));
}

size_t ca_mpsc_capacity(const ca_mpsc* queue) {
    return queue ? queue->cap : 0;
}

size_t ca_mpsc_size(const ca_mpsc* queue) {
    if (!queue) return 0;
    ca_error err;
    ca_error_init(&err);
    ca_mutex_lock(queue->mutex, &err);
    size_t wr = atomic_load_explicit(&queue->wr, memory_order_relaxed);
    size_t rd = atomic_load_explicit(&queue->rd, memory_order_relaxed);
    ca_mutex_unlock(queue->mutex, &err);
    return wr - rd;
}

bool ca_mpsc_empty(const ca_mpsc* queue) {
    if (!queue) return true;
    ca_error err;
    ca_error_init(&err);
    ca_mutex_lock(queue->mutex, &err);
    size_t wr = atomic_load_explicit(&queue->wr, memory_order_relaxed);
    size_t rd = atomic_load_explicit(&queue->rd, memory_order_relaxed);
    ca_mutex_unlock(queue->mutex, &err);
    return wr == rd;
}

ca_status ca_mpsc_push_copy(ca_mpsc* queue, const void* value) {
    if (!queue || !value) return CA_INVALID_ARGUMENT;

    ca_error err;
    ca_error_init(&err);
    ca_mutex_lock(queue->mutex, &err);
    size_t wr = atomic_load_explicit(&queue->wr, memory_order_relaxed);
    size_t rd = atomic_load_explicit(&queue->rd, memory_order_relaxed);
    size_t used = wr - rd;
    if (used >= queue->cap) {
        ca_mutex_unlock(queue->mutex, &err);
        return CA_OVERFLOW;
    }
    size_t idx = wr % queue->cap;
    memcpy(queue->buf + idx * queue->elem_size, value, queue->elem_size);
    atomic_store_explicit(&queue->wr, wr + 1, memory_order_release);
    ca_mutex_unlock(queue->mutex, &err);
    ca_cond_signal(queue->cond, &err);
    return CA_OK;
}

ca_status ca_mpsc_pop(ca_mpsc* queue, void* out) {
    if (!queue || !out) return CA_INVALID_ARGUMENT;

    ca_error err;
    ca_error_init(&err);
    ca_mutex_lock(queue->mutex, &err);
    size_t wr = atomic_load_explicit(&queue->wr, memory_order_relaxed);
    size_t rd = atomic_load_explicit(&queue->rd, memory_order_relaxed);
    if (wr == rd) {
        ca_mutex_unlock(queue->mutex, &err);
        return CA_INVALID_ARGUMENT;
    }
    size_t idx = rd % queue->cap;
    memcpy(out, queue->buf + idx * queue->elem_size, queue->elem_size);
    atomic_store_explicit(&queue->rd, rd + 1, memory_order_release);
    ca_mutex_unlock(queue->mutex, &err);
    return CA_OK;
}
