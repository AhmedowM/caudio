#include <caudio/runtime/shm.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

struct ca_shm {
#if defined(_WIN32) || defined(__CYGWIN__)
    HANDLE handle;
#else
    int fd;
#endif
    size_t size;
    int created;
};

ca_shm_t ca_shm_open(const char* name, size_t size, int create, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (name == nullptr || name[0] == '\0') {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "name is empty");
        }
        return CA_SHM_INVALID;
    }

#if defined(__ANDROID__)
    if (err) {
        ca_error_set(err, CA_NOT_SUPPORTED, "shared memory unsupported on Android");
    }
    (void)name;
    (void)size;
    (void)create;
    return CA_SHM_INVALID;
#elif defined(_WIN32) || defined(__CYGWIN__)
    ca_shm_t shm = (ca_shm_t)calloc(1, sizeof(struct ca_shm));
    if (shm == nullptr) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate shm");
        }
        return CA_SHM_INVALID;
    }

    DWORD flProtect = PAGE_READWRITE;
    (void)create;

    shm->handle =
        CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, flProtect, 0, (DWORD)size, nullptr);
    if (shm->handle == nullptr) {
        free(shm);
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "CreateFileMapping failed");
        }
        return CA_SHM_INVALID;
    }

    DWORD last = GetLastError();
    shm->created = (last != ERROR_ALREADY_EXISTS);
    shm->size = size;

    return shm;
#else
    ca_shm_t shm = (ca_shm_t)calloc(1, sizeof(struct ca_shm));
    if (shm == nullptr) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate shm");
        }
        return CA_SHM_INVALID;
    }

    int flags = O_RDWR;
    mode_t mode = 0600;

    if (create) {
        flags |= O_CREAT;
    }

    /* POSIX shm names must start with "/". */
    const char* shm_name = name;
    if (name[0] != '/') {
        /* Build "/name". */
        char* full = (char*)malloc(strlen(name) + 2);
        if (full == nullptr) {
            free(shm);
            if (err) {
                ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate name");
            }
            return CA_SHM_INVALID;
        }
        full[0] = '/';
        strcpy(full + 1, name);
        shm_name = full;
    }

    shm->fd = shm_open(shm_name, flags, mode);

    if (name[0] != '/') {
        free((void*)shm_name);
    }

    if (shm->fd < 0) {
        free(shm);
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "shm_open failed: %d", errno);
        }
        return CA_SHM_INVALID;
    }

    if (create && size > 0) {
        if (ftruncate(shm->fd, (off_t)size) != 0) {
            close(shm->fd);
            free(shm);
            if (err) {
                ca_error_set(err, CA_IO_ERROR, "ftruncate failed: %d", errno);
            }
            return CA_SHM_INVALID;
        }
        shm->created = 1;
    }

    /* Query current size. */
    struct stat st;
    if (fstat(shm->fd, &st) == 0) {
        shm->size = (size_t)st.st_size;
    } else {
        shm->size = size;
    }

    return shm;
#endif
}

ca_status ca_shm_map(ca_shm_t shm, size_t offset, size_t length, void** out, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (shm == CA_SHM_INVALID || out == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid shm or out");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(__ANDROID__)
    if (err) {
        ca_error_set(err, CA_NOT_SUPPORTED, "shared memory unsupported on Android");
    }
    (void)shm;
    (void)offset;
    (void)length;
    (void)out;
    return CA_NOT_SUPPORTED;
#elif defined(_WIN32) || defined(__CYGWIN__)
    DWORD dwDesired = FILE_MAP_READ | FILE_MAP_WRITE;
    void* addr = MapViewOfFile(shm->handle, dwDesired, 0, (DWORD)offset, length);
    if (addr == nullptr) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "MapViewOfFile failed");
        }
        return CA_IO_ERROR;
    }
    *out = addr;
#else
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_SHARED;

    if (length == 0) {
        length = shm->size - offset;
    }

    void* addr = mmap(nullptr, length, prot, flags, shm->fd, (off_t)offset);
    if (addr == MAP_FAILED) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "mmap failed: %d", errno);
        }
        return CA_IO_ERROR;
    }
    *out = addr;
#endif

    return CA_OK;
}

ca_status ca_shm_unmap(void* addr, size_t length, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (addr == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "addr is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    (void)length;

#if defined(__ANDROID__)
    if (err) {
        ca_error_set(err, CA_NOT_SUPPORTED, "shared memory unsupported on Android");
    }
    (void)addr;
    (void)length;
    return CA_NOT_SUPPORTED;
#elif defined(_WIN32) || defined(__CYGWIN__)
    if (!UnmapViewOfFile(addr)) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "UnmapViewOfFile failed");
        }
        return CA_IO_ERROR;
    }
#else
    /* length of 0 is a no-op; munmap(addr, 0) fails with EINVAL on macOS. */
    if (length == 0) {
        return CA_OK;
    }
    if (munmap(addr, length) != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "munmap failed: %d", errno);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}

ca_status ca_shm_close(ca_shm_t shm, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (shm == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "shm is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    if (shm == CA_SHM_INVALID) {
        return CA_OK;
    }

#if defined(__ANDROID__)
    free(shm);
#elif defined(_WIN32) || defined(__CYGWIN__)
    CloseHandle(shm->handle);
#else
    close(shm->fd);
#endif

    free(shm);
    return CA_OK;
}

ca_status ca_shm_unlink(const char* name, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (name == nullptr || name[0] == '\0') {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "name is empty");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(__ANDROID__)
    if (err) {
        ca_error_set(err, CA_NOT_SUPPORTED, "shared memory unsupported on Android");
    }
    (void)name;
    return CA_NOT_SUPPORTED;
#elif defined(_WIN32) || defined(__CYGWIN__)
    /* Win32 named file mappings are unlinked when the last handle closes;
       there is no separate unlink. */
    (void)name;
#else
    /* Build "/name" if needed. */
    const char* shm_name = name;
    char* full = nullptr;
    if (name[0] != '/') {
        full = (char*)malloc(strlen(name) + 2);
        if (full == nullptr) {
            if (err) {
                ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate name");
            }
            return CA_OUT_OF_MEMORY;
        }
        full[0] = '/';
        strcpy(full + 1, name);
        shm_name = full;
    }

    int rc = shm_unlink(shm_name);
    free(full);

    if (rc != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "shm_unlink failed: %d", errno);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}
