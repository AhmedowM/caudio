#include <caudio/runtime/path.h>

#include <cstdx/allocator.h>

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

static const char *find_last_separator(const char *path) {
    const char *last = nullptr;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            last = p;
        }
    }
    return last;
}

size_t ca_path_join(const char *base, const char *child, char *out,
                    size_t out_size, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (base == nullptr || child == nullptr || out == nullptr ||
        out_size == 0) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        }
        return (size_t)-1;
    }

    size_t base_len = strlen(base);
    size_t child_len = strlen(child);

    /* Determine if a separator is needed between base and child. */
    bool need_sep = false;
    if (base_len > 0) {
        char last = base[base_len - 1];
        if (last != '/' && last != '\\') {
            need_sep = true;
        }
    }

    /* Skip leading separators in child. */
    size_t child_start = 0;
    while (child_start < child_len &&
           (child[child_start] == '/' || child[child_start] == '\\')) {
        child_start++;
    }

    size_t needed = base_len;
    if (need_sep && child_start < child_len) {
        needed += 1; /* '/' */
    }
    needed += (child_len - child_start);

    if (needed + 1 > out_size) {
        if (err) {
            ca_error_set(err, CA_OVERFLOW, "path buffer too small");
        }
        return (size_t)-1;
    }

    size_t pos = 0;
    memcpy(out + pos, base, base_len);
    pos += base_len;

    if (need_sep && child_start < child_len) {
        out[pos++] = '/';
    }

    memcpy(out + pos, child + child_start, child_len - child_start);
    pos += child_len - child_start;
    out[pos] = '\0';

    return pos;
}

const char *ca_path_basename(const char *path) {
    if (path == nullptr) {
        return nullptr;
    }

    /* Find the end of the string, skipping trailing separators. */
    const char *end = path;
    while (*end != '\0') {
        ++end;
    }
    while (end > path && (end[-1] == '/' || end[-1] == '\\')) {
        --end;
    }

    if (end == path) {
        /* All separators or empty: return "/"-like result. */
        return path;
    }

    /* Walk back from end to find the last separator. */
    const char *start = end;
    while (start > path && (start[-1] != '/' && start[-1] != '\\')) {
        --start;
    }

    return start;
}

const char *ca_path_dirname(const char *path) {
    if (path == nullptr) {
        return nullptr;
    }

    const char *last = find_last_separator(path);
    if (last == nullptr) {
        return ".";
    }

    /* Return path up to (but not including) the separator. The result points
       into path; it is not NUL-terminated. The caller must copy it. */
    return path;
}

char *ca_path_normalize(char *path) {
    if (path == nullptr) {
        return nullptr;
    }

    for (char *p = path; *p != '\0'; ++p) {
        if (*p == '\\') {
            *p = '/';
        }
    }

    return path;
}

char *ca_path_identity_normalize(char *path) {
    if (path == nullptr) {
        return nullptr;
    }

    ca_path_normalize(path);
#if defined(_WIN32) || defined(__CYGWIN__)
    for (char *p = path; *p != '\0'; ++p) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = (char)(*p - 'A' + 'a');
        }
    }
#endif
    return path;
}

#if defined(_WIN32) || defined(__CYGWIN__)

/* Convert a wide temporary-directory path to canonical UTF-8. */
static ca_status path_temp_dir_wide(char *out, size_t out_size,
                                    size_t *out_len, ca_error *err) {
    const cx_allocator *alloc = cx_allocator_default();
    DWORD capacity = 512;
    WCHAR *wide = nullptr;
    DWORD copied = 0;

    for (;;) {
        size_t wide_size = (size_t)capacity * sizeof(WCHAR);
        wide = (WCHAR *)cx_allocator_alloc(alloc, wide_size, alignof(WCHAR));
        if (wide == nullptr) {
            if (err) {
                ca_error_set(err, CA_OUT_OF_MEMORY,
                             "failed to allocate temp path");
            }
            return CA_OUT_OF_MEMORY;
        }
        copied = GetTempPathW(capacity, wide);
        if (copied == 0) {
            cx_allocator_free(alloc, wide, wide_size, alignof(WCHAR));
            if (err) {
                ca_error_set(err, CA_PATH_ERROR,
                             "GetTempPathW failed (error %lu)",
                             (unsigned long)GetLastError());
            }
            return CA_PATH_ERROR;
        }
        if (copied < capacity) {
            break;
        }
        /* The return value is the required size, including the NUL. */
        cx_allocator_free(alloc, wide, wide_size, alignof(WCHAR));
        wide = nullptr;
        capacity = copied;
    }

    int needed = WideCharToMultiByte(CP_UTF8, 0, wide, (int)copied, nullptr, 0,
                                     nullptr, nullptr);
    if (needed <= 0) {
        cx_allocator_free(alloc, wide, (size_t)capacity * sizeof(WCHAR),
                          alignof(WCHAR));
        if (err) {
            ca_error_set(err, CA_PATH_ERROR, "temp path is not valid UTF-16");
        }
        return CA_PATH_ERROR;
    }

    size_t utf8_size = (size_t)needed + 1;
    char *utf8 = (char *)cx_allocator_alloc(alloc, utf8_size, 0);
    if (utf8 == nullptr) {
        cx_allocator_free(alloc, wide, (size_t)capacity * sizeof(WCHAR),
                          alignof(WCHAR));
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate temp path");
        }
        return CA_OUT_OF_MEMORY;
    }
    WideCharToMultiByte(CP_UTF8, 0, wide, (int)copied, utf8, needed, nullptr,
                        nullptr);
    cx_allocator_free(alloc, wide, (size_t)capacity * sizeof(WCHAR),
                      alignof(WCHAR));
    utf8[needed] = '\0';

    size_t length = (size_t)needed;
    /* GetTempPathW returns a trailing separator; drop it unless the result is
     * a drive or share root ("C:\" or "\\server\share\"). */
    while (length > 1 &&
           (utf8[length - 1] == '\\' || utf8[length - 1] == '/')) {
        if (length == 3 && utf8[1] == ':') {
            break;
        }
        length--;
    }
    utf8[length] = '\0';
    ca_path_normalize(utf8);

    ca_status status = CA_OK;
    if (out_len) {
        *out_len = length;
    }
    if (out != nullptr && out_size > 0) {
        if (length + 1 > out_size) {
            out[0] = '\0';
            status = CA_OVERFLOW;
            if (err) {
                ca_error_set(err, CA_OVERFLOW, "temp path buffer too small");
            }
        } else {
            memcpy(out, utf8, length + 1);
        }
    }
    cx_allocator_free(alloc, utf8, utf8_size, 0);
    return status;
}

#else

static ca_status path_temp_dir_posix(char *out, size_t out_size,
                                     size_t *out_len, ca_error *err) {
    const char *tmp = getenv("TMPDIR");
    if (tmp == nullptr || tmp[0] == '\0') {
        tmp = "/tmp";
    }

    size_t length = strlen(tmp);
    while (length > 1 && tmp[length - 1] == '/') {
        length--;
    }

    if (out_len) {
        *out_len = length;
    }
    if (out != nullptr && out_size > 0) {
        if (length + 1 > out_size) {
            out[0] = '\0';
            if (err) {
                ca_error_set(err, CA_OVERFLOW, "temp path buffer too small");
            }
            return CA_OVERFLOW;
        }
        memcpy(out, tmp, length);
        out[length] = '\0';
    }
    return CA_OK;
}

#endif

ca_status ca_path_temp_dir(char *out, size_t out_size, size_t *out_len,
                           ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (out_len) {
        *out_len = 0;
    }
    if (out == nullptr && out_size != 0) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT,
                         "out is NULL but out_size is non-zero");
        }
        return CA_INVALID_ARGUMENT;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    return path_temp_dir_wide(out, out_size, out_len, err);
#else
    return path_temp_dir_posix(out, out_size, out_len, err);
#endif
}
