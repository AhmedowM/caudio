#include <caudio/runtime/mutex.h>

#include <cstdx/allocator.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#endif

struct ca_mutex {
#if defined(_WIN32) || defined(__CYGWIN__)
    SRWLOCK lock;
#else
    pthread_mutex_t lock;
#endif
};

ca_status ca_mutex_create(ca_mutex_t* out, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (out == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    const cx_allocator* alloc = cx_allocator_default();
    ca_mutex_t m = (ca_mutex_t)cx_allocator_alloc(alloc, sizeof(struct ca_mutex), alignof(struct ca_mutex));
    if (m == nullptr) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate mutex");
        }
        return CA_OUT_OF_MEMORY;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    InitializeSRWLock(&m->lock);
#else
    int rc = pthread_mutex_init(&m->lock, nullptr);
    if (rc != 0) {
        cx_allocator_free(alloc, m, sizeof(struct ca_mutex), alignof(struct ca_mutex));
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_mutex_init failed: %d", rc);
        }
        return CA_IO_ERROR;
    }
#endif

    *out = m;
    return CA_OK;
}

void ca_mutex_destroy(ca_mutex_t mutex) {
    if (mutex == nullptr) {
        return;
    }

#if !defined(_WIN32) && !defined(__CYGWIN__)
    pthread_mutex_destroy(&mutex->lock);
#endif

    const cx_allocator* alloc = cx_allocator_default();
    cx_allocator_free(alloc, mutex, sizeof(struct ca_mutex), alignof(struct ca_mutex));
}

ca_status ca_mutex_lock(ca_mutex_t mutex, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (mutex == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "mutex is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    AcquireSRWLockExclusive(&mutex->lock);
#else
    int rc = pthread_mutex_lock(&mutex->lock);
    if (rc != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_mutex_lock failed: %d", rc);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}

ca_status ca_mutex_trylock(ca_mutex_t mutex, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (mutex == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "mutex is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    if (!TryAcquireSRWLockExclusive(&mutex->lock)) {
        if (err) {
            ca_error_set(err, CA_BUSY, "mutex is busy");
        }
        return CA_BUSY;
    }
#else
    int rc = pthread_mutex_trylock(&mutex->lock);
    if (rc == EBUSY) {
        if (err) {
            ca_error_set(err, CA_BUSY, "mutex is busy");
        }
        return CA_BUSY;
    }

    if (rc != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_mutex_trylock failed: %d", rc);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}

ca_status ca_mutex_unlock(ca_mutex_t mutex, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (mutex == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "mutex is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    ReleaseSRWLockExclusive(&mutex->lock);
#else
    int rc = pthread_mutex_unlock(&mutex->lock);
    if (rc != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_mutex_unlock failed: %d", rc);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}

void* ca_mutex_native_handle(ca_mutex_t mutex) {
    if (mutex == nullptr) {
        return nullptr;
    }
    return (void*)&mutex->lock;
}
