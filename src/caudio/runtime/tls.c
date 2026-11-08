#include <caudio/runtime/tls.h>
#include <stdlib.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#endif

struct ca_tls_key {
#if defined(_WIN32) || defined(__CYGWIN__)
    DWORD tls_index;
#else
    pthread_key_t key;
#endif
    ca_tls_destructor destructor;
};

ca_status ca_tls_key_create(ca_tls_key_t* out, ca_tls_destructor dest, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (out == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    ca_tls_key_t key = (ca_tls_key_t)calloc(1, sizeof(struct ca_tls_key));
    if (key == nullptr) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate tls key");
        }
        return CA_OUT_OF_MEMORY;
    }

    key->destructor = dest;

#if defined(_WIN32) || defined(__CYGWIN__)
    /* Win32 TLS does not support per-key destructors; the destructor is
       stored but only honored on POSIX. */
    DWORD index = TlsAlloc();
    if (index == TLS_OUT_OF_INDEXES) {
        free(key);
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "TlsAlloc failed");
        }
        return CA_IO_ERROR;
    }
    key->tls_index = index;
#else
    int rc = pthread_key_create(&key->key, dest);
    if (rc != 0) {
        free(key);
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_key_create failed: %d", rc);
        }
        return CA_IO_ERROR;
    }
#endif

    *out = key;
    return CA_OK;
}

void ca_tls_key_destroy(ca_tls_key_t key) {
    if (key == nullptr) {
        return;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    TlsFree(key->tls_index);
#else
    pthread_key_delete(key->key);
#endif

    free(key);
}

ca_status ca_tls_set(ca_tls_key_t key, void* value, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (key == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "key is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    if (!TlsSetValue(key->tls_index, value)) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "TlsSetValue failed");
        }
        return CA_IO_ERROR;
    }
#else
    int rc = pthread_setspecific(key->key, value);
    if (rc != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "pthread_setspecific failed: %d", rc);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}

void* ca_tls_get(ca_tls_key_t key, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (key == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "key is nullptr");
        }
        return nullptr;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    return TlsGetValue(key->tls_index);
#else
    return pthread_getspecific(key->key);
#endif
}
