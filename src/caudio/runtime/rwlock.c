/* POSIX.1-2008 for pthread_rwlock_* with -std=c2x on Linux/GCC */
#define _POSIX_C_SOURCE 200809L

#include <caudio/runtime/rwlock.h>

#include <stdlib.h>

#if defined(_WIN32) || defined(__CYGWIN__)
    #include <windows.h>
#else
    #include <pthread.h>
    #include <errno.h>
#endif

struct ca_rwlock {
#if defined(_WIN32) || defined(__CYGWIN__)
    SRWLOCK lock;
#else
    pthread_rwlock_t lock;
#endif
};

ca_status ca_rwlock_create(ca_rwlock_t *out, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (out == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is NULL");
        }
        return CA_INVALID_ARGUMENT;
    }

    ca_rwlock_t rw = (ca_rwlock_t)calloc(1, sizeof(struct ca_rwlock));
    if (rw == nullptr) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate rwlock");
        }
        return CA_OUT_OF_MEMORY;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    InitializeSRWLock(&rw->lock);
#else
    int rc = pthread_rwlock_init(&rw->lock, nullptr);
    if (rc != 0) {
        free(rw);
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_rwlock_init failed: %d",
                         rc);
        }
        return CA_IO_ERROR;
    }
#endif

    *out = rw;
    return CA_OK;
}

void ca_rwlock_destroy(ca_rwlock_t rwlock) {
    if (rwlock == nullptr) {
        return;
    }

#if !defined(_WIN32) && !defined(__CYGWIN__)
    pthread_rwlock_destroy(&rwlock->lock);
#endif

    free(rwlock);
}

ca_status ca_rwlock_rdlock(ca_rwlock_t rwlock, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (rwlock == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "rwlock is NULL");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    AcquireSRWLockShared(&rwlock->lock);
#else
    int rc = pthread_rwlock_rdlock(&rwlock->lock);
    if (rc != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_rwlock_rdlock failed: %d",
                         rc);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}

ca_status ca_rwlock_wrlock(ca_rwlock_t rwlock, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (rwlock == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "rwlock is NULL");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    AcquireSRWLockExclusive(&rwlock->lock);
#else
    int rc = pthread_rwlock_wrlock(&rwlock->lock);
    if (rc != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_rwlock_wrlock failed: %d",
                         rc);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}

ca_status ca_rwlock_unlock(ca_rwlock_t rwlock, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (rwlock == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "rwlock is NULL");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    ReleaseSRWLockShared(&rwlock->lock);
#else
    int rc = pthread_rwlock_unlock(&rwlock->lock);
    if (rc != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_rwlock_unlock failed: %d",
                         rc);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}
