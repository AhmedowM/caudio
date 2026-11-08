#include <caudio/runtime/file_lock.h>
#include <stdlib.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

ca_status ca_file_lock(void* fd, ca_file_lock_type type, int64_t offset, int64_t length, int block,
                       ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (fd == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "fd is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    (void)length;

#if defined(_WIN32) || defined(__CYGWIN__)
    HANDLE h = (HANDLE)fd;
    (void)block;
    /* Always use fail-immediately for simplicity and cross-platform
       consistency. A blocking lock can be implemented by retrying. */
    DWORD dwFlags = LOCKFILE_FAIL_IMMEDIATELY;
    DWORD dwType = (type == CA_FILE_LOCK_WRITE) ? LOCKFILE_EXCLUSIVE_LOCK : 0; /* shared */

    /* LockFileEx with a synchronous handle can be problematic on some
       MinGW-w64 toolchains. Use LockFile for blocking locks instead. */
    if (block) {
        DWORD lockLow = (length == 0) ? 0xFFFFFFFF : (DWORD)(length & 0xFFFFFFFFULL);
        DWORD lockHigh = (length == 0) ? 0xFFFFFFFF : (DWORD)((length >> 32) & 0xFFFFFFFFULL);
        DWORD offsetLow = (DWORD)(offset & 0xFFFFFFFFULL);
        DWORD offsetHigh = (DWORD)((offset >> 32) & 0xFFFFFFFFULL);
        if (!LockFile(h, offsetLow, offsetHigh, lockLow, lockHigh)) {
            DWORD last = GetLastError();
            if (last == ERROR_LOCK_VIOLATION) {
                if (err) {
                    ca_error_set(err, CA_BUSY, "file region is locked");
                }
                return CA_BUSY;
            }
            if (err) {
                ca_error_set(err, CA_IO_ERROR, "LockFile failed: %lu", last);
            }
            return CA_IO_ERROR;
        }
    } else {
        OVERLAPPED ol = {0};
        ol.Offset = (DWORD)(offset & 0xFFFFFFFFULL);
        ol.OffsetHigh = (DWORD)((offset >> 32) & 0xFFFFFFFFULL);
        DWORD lockLow = (length == 0) ? 0xFFFFFFFF : (DWORD)(length & 0xFFFFFFFFULL);
        DWORD lockHigh = (length == 0) ? 0xFFFFFFFF : (DWORD)((length >> 32) & 0xFFFFFFFFULL);
        if (!LockFileEx(h, dwFlags, dwType, lockLow, lockHigh, &ol)) {
            DWORD last = GetLastError();
            if (last == ERROR_LOCK_VIOLATION) {
                if (err) {
                    ca_error_set(err, CA_BUSY, "file region is locked");
                }
                return CA_BUSY;
            }
            if (err) {
                ca_error_set(err, CA_IO_ERROR, "LockFileEx failed: %lu", last);
            }
            return CA_IO_ERROR;
        }
    }
#else
    int f = (int)(intptr_t)fd;
    struct flock fl = {0};
    fl.l_type = (type == CA_FILE_LOCK_WRITE) ? F_WRLCK : F_RDLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = (off_t)offset;
    fl.l_len = (off_t)length;

    int cmd = block ? F_SETLKW : F_SETLK;
    if (fcntl(f, cmd, &fl) != 0) {
        if (errno == EACCES || errno == EAGAIN) {
            if (err) {
                ca_error_set(err, CA_BUSY, "file region is locked");
            }
            return CA_BUSY;
        }
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "fcntl failed: %d", errno);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}

ca_status ca_file_unlock(void* fd, int64_t offset, int64_t length, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (fd == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "fd is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    HANDLE h = (HANDLE)fd;
    /* Unlock from ol.Offset for length bytes. Use max DWORD to unlock to EOF. */
    DWORD unlockLow = (length == 0) ? 0xFFFFFFFF : (DWORD)(length & 0xFFFFFFFFULL);
    DWORD unlockHigh = (length == 0) ? 0xFFFFFFFF : (DWORD)((length >> 32) & 0xFFFFFFFFULL);
    DWORD offsetLow = (DWORD)(offset & 0xFFFFFFFFULL);
    DWORD offsetHigh = (DWORD)((offset >> 32) & 0xFFFFFFFFULL);
    if (!UnlockFile(h, offsetLow, offsetHigh, unlockLow, unlockHigh)) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "UnlockFile failed");
        }
        return CA_IO_ERROR;
    }
#else
    int f = (int)(intptr_t)fd;
    struct flock fl = {0};
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = (off_t)offset;
    fl.l_len = (off_t)length;

    if (fcntl(f, F_SETLK, &fl) != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "fcntl unlock failed: %d", errno);
        }
        return CA_IO_ERROR;
    }
#endif

    return CA_OK;
}
