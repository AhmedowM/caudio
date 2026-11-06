#include <caudio/runtime/cond.h>
#include <caudio/runtime/mutex.h>

#include <stdlib.h>

#if defined(_WIN32) || defined(__CYGWIN__)
    #include <windows.h>
#else
    #include <pthread.h>
    #include <errno.h>
    #include <time.h>
#endif

struct ca_cond {
#if defined(_WIN32) || defined(__CYGWIN__)
    CONDITION_VARIABLE cv;
#else
    pthread_cond_t cond;
#endif
};

ca_status ca_cond_create(ca_cond_t *out, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (out == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is NULL");
        }
        return CA_INVALID_ARGUMENT;
    }

    ca_cond_t c = (ca_cond_t)calloc(1, sizeof(struct ca_cond));
    if (c == nullptr) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate cond");
        }
        return CA_OUT_OF_MEMORY;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    InitializeConditionVariable(&c->cv);
#else
    int rc = pthread_cond_init(&c->cond, nullptr);
    if (rc != 0) {
        free(c);
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_cond_init failed: %d",
                         rc);
        }
        return CA_IO_ERROR;
    }
#endif

    *out = c;
    return CA_OK;
}

void ca_cond_destroy(ca_cond_t cond) {
    if (cond == nullptr) {
        return;
    }

#if !defined(_WIN32) && !defined(__CYGWIN__)
    pthread_cond_destroy(&cond->cond);
#endif

    free(cond);
}

ca_status ca_cond_signal(ca_cond_t cond, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (cond == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "cond is NULL");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    WakeConditionVariable(&cond->cv);
#else
    int rc = pthread_cond_signal(&cond->cond);
    if (rc != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_cond_signal failed: %d",
                         rc);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}

ca_status ca_cond_broadcast(ca_cond_t cond, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (cond == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "cond is NULL");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    WakeAllConditionVariable(&cond->cv);
#else
    int rc = pthread_cond_broadcast(&cond->cond);
    if (rc != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_cond_broadcast failed: %d",
                         rc);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}

ca_status ca_cond_wait(ca_cond_t cond, ca_mutex_t mutex, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (cond == nullptr || mutex == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "cond or mutex is NULL");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    /* Win32 condition variables need the SRWLOCK handle. */
    if (!SleepConditionVariableSRW(&cond->cv,
                                   (SRWLOCK *)ca_mutex_native_handle(mutex),
                                   INFINITE, 0)) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR,
                         "SleepConditionVariableSRW failed");
        }
        return CA_IO_ERROR;
    }
#else
    int rc = pthread_cond_wait(&cond->cond, &mutex->lock);
    if (rc != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_cond_wait failed: %d",
                         rc);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}

ca_status ca_cond_timedwait(ca_cond_t cond, ca_mutex_t mutex,
                            ca_time_us_t abstime_us, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (cond == nullptr || mutex == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "cond or mutex is NULL");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    /* Convert absolute microseconds to a relative millisecond timeout. */
    ca_time_us_t now;
    if (ca_time_now(&now, nullptr) != CA_OK) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "ca_time_now failed");
        }
        return CA_IO_ERROR;
    }

    ca_time_us_t remaining = (abstime_us > now) ? (abstime_us - now) : 0;
    DWORD ms = (DWORD)((remaining + 999ULL) / 1000ULL);

    if (!SleepConditionVariableSRW(&cond->cv,
                                   (SRWLOCK *)ca_mutex_native_handle(mutex),
                                   ms, 0)) {
        DWORD errCode = GetLastError();
        if (errCode == ERROR_TIMEOUT) {
            if (err) {
                ca_error_set(err, CA_TIMED_OUT, "condition variable timed out");
            }
            return CA_TIMED_OUT;
        }

        if (err) {
            ca_error_set(err, CA_IO_ERROR, "SleepConditionVariableSRW failed");
        }
        return CA_IO_ERROR;
    }
#else
    struct timespec ts;
    ts.tv_sec  = (time_t)(abstime_us / 1000000ULL);
    ts.tv_nsec = (long)((abstime_us % 1000000ULL) * 1000ULL);

    int rc = pthread_cond_timedwait(&cond->cond, &mutex->lock, &ts);
    if (rc == ETIMEDOUT) {
        if (err) {
            ca_error_set(err, CA_TIMED_OUT, "condition variable timed out");
        }
        return CA_TIMED_OUT;
    }

    if (rc != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_cond_timedwait failed: %d",
                         rc);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}
