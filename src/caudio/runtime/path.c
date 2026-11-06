#include <caudio/runtime/path.h>

#include <string.h>

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

    const char *last = find_last_separator(path);
    if (last == nullptr) {
        return path;
    }

    /* Skip trailing separators. */
    const char *p = last + 1;
    while (*p == '/' || *p == '\\') {
        ++p;
    }

    if (*p == '\0') {
        /* Path ended with separator(s); walk back to find the real basename. */
        const char *end = last;
        while (end > path && (end[-1] == '/' || end[-1] == '\\')) {
            --end;
        }

        const char *prev = find_last_separator(path);
        if (prev == nullptr || prev >= end) {
            return path;
        }

        return prev + 1;
    }

    return p;
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
