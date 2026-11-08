#include <caudio/runtime/fs.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <dirent.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

struct ca_file {
#if defined(_WIN32) || defined(__CYGWIN__)
    HANDLE handle;
#else
    int fd;
#endif
    bool writable;
};

#if !defined(_WIN32) && !defined(__CYGWIN__)
static const char* fs_open_mode(uint32_t flags, char* mode_buf, size_t mode_buf_size) {
    const char* mode = nullptr;

    bool read = (flags & CA_FS_READ) != 0;
    bool write = (flags & CA_FS_WRITE) != 0;
    bool append = (flags & CA_FS_APPEND) != 0;
    bool create = (flags & CA_FS_CREATE) != 0;
    bool truncate = (flags & CA_FS_TRUNCATE) != 0;

    if (append) {
        mode = create ? "a+" : "a";
    } else if (read && write) {
        mode = create ? "w+" : "r+";
        if (truncate) {
            mode = "w+";
        }
    } else if (read) {
        mode = "r";
    } else if (write) {
        mode = create ? "w" : (truncate ? "w" : nullptr);
        if (!mode) {
            mode = nullptr;
        }
    } else if (create) {
        mode = truncate ? "w" : nullptr;
    }

    if (mode && mode_buf) {
        snprintf(mode_buf, mode_buf_size, "%s", mode);
    }

    return mode;
}
#endif

ca_file_t ca_fs_open(const char* path, uint32_t flags, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (path == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "path is nullptr");
        }
        return CA_FS_INVALID;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    /* Build a wide-character path from UTF-8. */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (wlen <= 0) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid UTF-8 path");
        }
        return CA_FS_INVALID;
    }

    LPWSTR wpath = (LPWSTR)malloc((size_t)wlen * sizeof(WCHAR));
    if (wpath == nullptr) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate path");
        }
        return CA_FS_INVALID;
    }

    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);

    DWORD dwDesiredAccess = 0;
    DWORD dwShareMode = FILE_SHARE_READ;
    DWORD dwCreationDisposition = 0;
    bool read = (flags & CA_FS_READ) != 0;
    bool write = (flags & CA_FS_WRITE) != 0;
    bool append = (flags & CA_FS_APPEND) != 0;
    bool create = (flags & CA_FS_CREATE) != 0;
    bool truncate = (flags & CA_FS_TRUNCATE) != 0;

    if (read) {
        dwDesiredAccess |= GENERIC_READ;
    }
    if (write || append) {
        dwDesiredAccess |= GENERIC_WRITE;
        dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
    }

    if (create && truncate) {
        dwCreationDisposition = CREATE_ALWAYS;
    } else if (create) {
        dwCreationDisposition = OPEN_ALWAYS;
    } else if (truncate) {
        dwCreationDisposition = TRUNCATE_EXISTING;
    } else {
        dwCreationDisposition = OPEN_EXISTING;
    }

    DWORD flagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
    if (append) {
        flagsAndAttributes = FILE_FLAG_SEQUENTIAL_SCAN;
    }

    HANDLE h = CreateFileW(wpath, dwDesiredAccess, dwShareMode, nullptr, dwCreationDisposition,
                           flagsAndAttributes, nullptr);
    free(wpath);

    if (h == INVALID_HANDLE_VALUE) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "CreateFileW failed for %s", path);
        }
        return CA_FS_INVALID;
    }

    ca_file_t file = (ca_file_t)calloc(1, sizeof(struct ca_file));
    if (file == nullptr) {
        CloseHandle(h);
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate file");
        }
        return CA_FS_INVALID;
    }

    file->handle = h;
    file->writable = write || append;
    return file;
#else
    char mode_buf[8] = {0};
    const char* mode = fs_open_mode(flags, mode_buf, sizeof(mode_buf));
    if (mode == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid open flags");
        }
        return CA_FS_INVALID;
    }

    FILE* fp = fopen(path, mode);
    if (fp == nullptr) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "fopen failed for %s: %s", path, strerror(errno));
        }
        return CA_FS_INVALID;
    }

    ca_file_t file = (ca_file_t)calloc(1, sizeof(struct ca_file));
    if (file == nullptr) {
        fclose(fp);
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate file");
        }
        return CA_FS_INVALID;
    }

    file->fd = dup(fileno(fp));
    fclose(fp);
    /* We keep the fd directly; use POSIX fd ops for I/O. */
    file->writable = (flags & (CA_FS_WRITE | CA_FS_APPEND | CA_FS_CREATE)) != 0;

    return file;
#endif
}

size_t ca_fs_read(ca_file_t file, void* buf, size_t size, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (file == CA_FS_INVALID || buf == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid file or buffer");
        }
        return (size_t)-1;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    DWORD read = 0;
    if (!ReadFile(file->handle, buf, (DWORD)size, &read, nullptr)) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "ReadFile failed");
        }
        return (size_t)-1;
    }
    return (size_t)read;
#else
    ssize_t n = read(file->fd, buf, size);
    if (n < 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "read failed: %s", strerror(errno));
        }
        return (size_t)-1;
    }
    return (size_t)n;
#endif
}

size_t ca_fs_write(ca_file_t file, const void* buf, size_t size, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (file == CA_FS_INVALID || buf == nullptr) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid file or buffer");
        }
        return (size_t)-1;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    DWORD written = 0;
    if (!WriteFile(file->handle, buf, (DWORD)size, &written, nullptr)) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "WriteFile failed");
        }
        return (size_t)-1;
    }
    return (size_t)written;
#else
    ssize_t n = write(file->fd, buf, size);
    if (n < 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "write failed: %s", strerror(errno));
        }
        return (size_t)-1;
    }
    return (size_t)n;
#endif
}

void ca_fs_close(ca_file_t file) {
    if (file == CA_FS_INVALID) {
        return;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    if (file->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(file->handle);
    }
#else
    if (file->fd >= 0) {
        close(file->fd);
    }
#endif

    free(file);
}

uint64_t ca_fs_size(ca_file_t file, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (file == CA_FS_INVALID) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid file");
        }
        return (uint64_t)-1;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    LARGE_INTEGER li;
    if (!GetFileSizeEx(file->handle, &li)) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "GetFileSizeEx failed");
        }
        return (uint64_t)-1;
    }
    return (uint64_t)li.QuadPart;
#else
    off_t off = lseek(file->fd, 0, SEEK_END);
    if (off < 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "lseek failed: %s", strerror(errno));
        }
        return (uint64_t)-1;
    }
    return (uint64_t)off;
#endif
}

bool ca_fs_exists(const char* path, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (wlen <= 0) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid UTF-8 path");
        }
        return false;
    }

    LPWSTR wpath = (LPWSTR)malloc((size_t)wlen * sizeof(WCHAR));
    if (wpath == nullptr) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate path");
        }
        return false;
    }

    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);
    DWORD attrs = GetFileAttributesW(wpath);
    free(wpath);

    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return true;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    return true;
#endif
}

ca_status ca_fs_mkdirs(const char* path, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (path == nullptr || path[0] == '\0') {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "empty path");
        }
        return CA_INVALID_ARGUMENT;
    }

    /* Build the path component by component. */
    size_t len = strlen(path);
    char* buf = (char*)malloc(len + 1);
    if (buf == nullptr) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate path");
        }
        return CA_OUT_OF_MEMORY;
    }

    ca_status status = CA_OK;
    size_t i = 0;

    /* Skip leading separators. */
    while (i < len && (path[i] == '/' || path[i] == '\\')) {
        i++;
    }

    while (i <= len) {
        /* Find the next separator. */
        size_t j = i;
        while (j < len && path[j] != '/' && path[j] != '\\') {
            j++;
        }

        size_t component_len = j - i;
        if (component_len == 0) {
            if (j >= len) {
                break;
            }
            i = j + 1;
            continue;
        }

        /* Copy path[0..j) into buf. */
        memcpy(buf, path, j);
        buf[j] = '\0';

        /* Skip "." and ".." components. */
        if ((component_len == 1 && buf[i == 0 ? 0 : j - 1] == '.') ||
            (component_len == 2 && buf[j - 2] == '.' && buf[j - 1] == '.')) {
            i = j + 1;
            continue;
        }

#if defined(_WIN32) || defined(__CYGWIN__)
        int wlen = MultiByteToWideChar(CP_UTF8, 0, buf, -1, nullptr, 0);
        if (wlen <= 0) {
            status = CA_INVALID_ARGUMENT;
            if (err) {
                ca_error_set(err, CA_INVALID_ARGUMENT, "invalid UTF-8 path");
            }
            break;
        }

        LPWSTR wbuf = (LPWSTR)malloc((size_t)wlen * sizeof(WCHAR));
        if (wbuf == nullptr) {
            status = CA_OUT_OF_MEMORY;
            if (err) {
                ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate path");
            }
            break;
        }

        MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, wlen);

        if (!CreateDirectoryW(wbuf, nullptr)) {
            DWORD errCode = GetLastError();
            if (errCode != ERROR_ALREADY_EXISTS) {
                status = CA_IO_ERROR;
                if (err) {
                    ca_error_set(err, CA_IO_ERROR, "CreateDirectoryW failed for %s", buf);
                }
                free(wbuf);
                break;
            }
        }
        free(wbuf);
#else
        if (mkdir(buf, 0755) != 0) {
            if (errno != EEXIST) {
                status = CA_IO_ERROR;
                if (err) {
                    ca_error_set(err, CA_IO_ERROR, "mkdir failed for %s: %s", buf, strerror(errno));
                }
                break;
            }
        }
#endif

        if (j >= len) {
            break;
        }

        i = j + 1;
    }

    free(buf);
    return status;
}

#if !defined(_WIN32) && !defined(__CYGWIN__)
/* Remove all entries inside a directory (not the directory itself).
 * Used as a fallback when rmdir fails with ENOTEMPTY. */
static ca_status remove_dir_contents(const char* path, ca_error* err) {
    DIR* dir = opendir(path);
    if (!dir) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "opendir failed for %s: %s", path, strerror(errno));
        }
        return CA_IO_ERROR;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        size_t child_len = strlen(path) + 1 + strlen(entry->d_name) + 1;
        char* child = (char*)malloc(child_len);
        if (!child) {
            closedir(dir);
            if (err) {
                ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate path");
            }
            return CA_OUT_OF_MEMORY;
        }

        snprintf(child, child_len, "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(child, &st) != 0) {
            free(child);
            continue;
        }

        ca_status status = CA_OK;
        if (S_ISDIR(st.st_mode)) {
            status = remove_dir_contents(child, err);
            if (status == CA_OK && rmdir(child) != 0) {
                status = CA_IO_ERROR;
                if (err) {
                    ca_error_set(err, CA_IO_ERROR, "rmdir failed for %s: %s", child,
                                 strerror(errno));
                }
            }
        } else {
            if (unlink(child) != 0) {
                status = CA_IO_ERROR;
                if (err) {
                    ca_error_set(err, CA_IO_ERROR, "unlink failed for %s: %s", child,
                                 strerror(errno));
                }
            }
        }

        free(child);
        if (status != CA_OK) {
            closedir(dir);
            return status;
        }
    }

    closedir(dir);
    return CA_OK;
}
#endif

ca_status ca_fs_remove(const char* path, ca_error* err) {
    if (err) {
        ca_error_init(err);
    }

    if (path == nullptr || path[0] == '\0') {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "empty path");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (wlen <= 0) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid UTF-8 path");
        }
        return CA_INVALID_ARGUMENT;
    }

    LPWSTR wpath = (LPWSTR)malloc((size_t)wlen * sizeof(WCHAR));
    if (wpath == nullptr) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate path");
        }
        return CA_OUT_OF_MEMORY;
    }

    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);

    bool ok = DeleteFileW(wpath);
    if (!ok) {
        /* Try as a directory (must be empty). */
        ok = RemoveDirectoryW(wpath);
    }
    free(wpath);

    if (!ok) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "failed to remove %s", path);
        }
        return CA_IO_ERROR;
    }
#else
    /* Try unlink first (works for files; fails with EISDIR for dirs). */
    if (unlink(path) == 0) {
        return CA_OK;
    }

    /* Try rmdir (works for empty directories). */
    if (rmdir(path) == 0) {
        return CA_OK;
    }

    /* Directory may not be empty; remove contents and retry. */
    ca_status st = remove_dir_contents(path, err);
    if (st != CA_OK) {
        return st;
    }

    if (rmdir(path) != 0) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "rmdir failed for %s: %s", path, strerror(errno));
        }
        return CA_IO_ERROR;
    }

    return CA_OK;
#endif

    return CA_OK;
}
