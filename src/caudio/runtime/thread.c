#include <caudio/runtime/thread.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <process.h>
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#endif

#include <stdlib.h>

struct ca_thread {
#if defined(_WIN32) || defined(__CYGWIN__)
    HANDLE handle;
    DWORD id;
#else
    pthread_t handle;
#endif
    ca_thread_fn fn;
    void* arg;
    void* ret;
    int started;
};

#if defined(_WIN32) || defined(__CYGWIN__)
static DWORD WINAPI thread_proc(LPVOID param) {
    struct ca_thread* t = (struct ca_thread*)param;
    t->ret = t->fn(t->arg);
    return 0;
}
#endif

ca_status ca_thread_create(ca_thread_t* out, ca_thread_fn fn, void* arg, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (out == nullptr || fn == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out or fn is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    ca_thread_t t = (ca_thread_t)calloc(1, sizeof(struct ca_thread));
    if (t == nullptr) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate thread");
        }
        return CA_OUT_OF_MEMORY;
    }

    t->fn = fn;
    t->arg = arg;

#if defined(_WIN32) || defined(__CYGWIN__)
    t->handle = CreateThread(nullptr, 0, thread_proc, t, 0, &t->id);
    if (t->handle == nullptr) {
        free(t);
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "CreateThread failed");
        }
        return CA_IO_ERROR;
    }
#else
    int rc = pthread_create(&t->handle, nullptr, fn, arg);
    if (rc != 0) {
        free(t);
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_create failed: %d", rc);
        }
        return CA_IO_ERROR;
    }
    t->started = 1;
#endif

    *out = t;
    return CA_OK;
}

ca_status ca_thread_join(ca_thread_t thread, void** out_ret, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (thread == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "thread is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    if (WaitForSingleObject(thread->handle, INFINITE) != WAIT_OBJECT_0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "WaitForSingleObject failed");
        }
        return CA_IO_ERROR;
    }

    if (out_ret != nullptr) {
        *out_ret = thread->ret;
    }

    CloseHandle(thread->handle);
#else
    void* ret = nullptr;
    int rc = pthread_join(thread->handle, &ret);
    if (rc != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_join failed: %d", rc);
        }
        return CA_IO_ERROR;
    }

    if (out_ret != nullptr) {
        *out_ret = ret;
    }
#endif

    free(thread);
    return CA_OK;
}

ca_status ca_thread_detach(ca_thread_t thread, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (thread == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "thread is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    /* Win32 threads cannot be detached; close the handle to release it.
       The thread continues running until it exits. */
    if (!CloseHandle(thread->handle)) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "CloseHandle failed");
        }
        return CA_IO_ERROR;
    }
#else
    int rc = pthread_detach(thread->handle);
    if (rc != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_detach failed: %d", rc);
        }
        return CA_IO_ERROR;
    }
#endif

    free(thread);
    return CA_OK;
}

void ca_thread_yield(void) {
#if defined(_WIN32) || defined(__CYGWIN__)
    SwitchToThread();
#else
    sched_yield();
#endif
}

uint64_t ca_thread_self(void) {
#if defined(_WIN32) || defined(__CYGWIN__)
    return (uint64_t)(uintptr_t)GetCurrentThreadId();
#else
    return (uint64_t)(uintptr_t)pthread_self();
#endif
}
