#include <caudio/config/paths.h>

#include <caudio/runtime/env.h>
#include <caudio/runtime/fs.h>
#include <caudio/runtime/path.h>

#include <cstdx/string_view.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Internal helpers                                                          */
/* ------------------------------------------------------------------------- */

static ca_status ca_from_cx(cx_status status) {
    switch (status) {
    case CX_OK:
        return CA_OK;
    case CX_OUT_OF_MEMORY:
        return CA_OUT_OF_MEMORY;
    case CX_INVALID_ARGUMENT:
        return CA_INVALID_ARGUMENT;
    case CX_OVERFLOW:
        return CA_OVERFLOW;
    case CX_OUT_OF_RANGE:
        return CA_INVALID_ARGUMENT;
    default:
        return CA_INVALID_ARGUMENT;
    }
}

static ca_status text_assign(cx_string *str, const char *text) {
    return ca_from_cx(cx_string_assign_view(
        str, cx_string_view_from_cstr(text ? text : "")));
}

static ca_status text_append(cx_string *str, const char *text) {
    return ca_from_cx(cx_string_append_cstr(str, text ? text : ""));
}

/* Copy a non-empty environment variable into @p out. */
static bool env_nonempty_to_string(const char *name, cx_string *out) {
    size_t length = 0;
    ca_error err;
    if (ca_env_get(name, nullptr, 0, &length, &err) != CA_OK || length == 0) {
        return false;
    }
    if (cx_string_resize(out, length) != CX_OK) {
        return false;
    }
    if (ca_env_get(name, cx_string_at_mut(out, 0), length + 1, &length, &err) !=
        CA_OK) {
        return false;
    }
    return true;
}

/* Remove trailing separators, but keep a lone root separator. */
static void strip_trailing_separators(cx_string *str) {
    size_t length = cx_string_length(str);
    while (length > 1) {
        char last = *cx_string_at(str, length - 1);
        if (last != '/' && last != '\\') {
            break;
        }
        (void)cx_string_pop_back(str, nullptr);
        length--;
    }
}

static ca_status join_child(cx_string *str, const char *child) {
    strip_trailing_separators(str);
    size_t length = cx_string_length(str);
    if (length > 0 && !(length == 1 && *cx_string_at(str, 0) == '/')) {
        ca_status status = text_append(str, "/");
        if (status != CA_OK) {
            return status;
        }
    }
    return text_append(str, child);
}

static ca_status temp_dir_to_string(cx_string *out) {
    size_t length = 0;
    ca_error err;
    ca_status status = ca_path_temp_dir(nullptr, 0, &length, &err);
    if (status != CA_OK) {
        return status;
    }
    if (length == 0) {
        cx_string_clear(out);
        return CA_OK;
    }
    if (cx_string_resize(out, length) != CX_OK) {
        return CA_OUT_OF_MEMORY;
    }
    return ca_path_temp_dir(cx_string_at_mut(out, 0), length + 1, &length,
                            &err);
}

/* Append the platform's per-user application directory to @p out. */
static ca_status data_base(cx_string *out) {
#if defined(_WIN32) || defined(__CYGWIN__)
    if (env_nonempty_to_string("LOCALAPPDATA", out)) {
        return join_child(out, "caudio");
    }
#endif
    if (env_nonempty_to_string("XDG_DATA_HOME", out)) {
        return join_child(out, "caudio");
    }
    if (env_nonempty_to_string("HOME", out) ||
        env_nonempty_to_string("USERPROFILE", out)) {
        ca_status status = join_child(out, ".local");
        if (status == CA_OK) {
            status = join_child(out, "share");
        }
        if (status == CA_OK) {
            status = join_child(out, "caudio");
        }
        return status;
    }
    ca_status status = temp_dir_to_string(out);
    if (status != CA_OK) {
        status = text_assign(out, "/tmp");
    }
    if (status != CA_OK) {
        return status;
    }
    return join_child(out, "caudio");
}

static ca_status config_base(cx_string *out) {
#if defined(_WIN32) || defined(__CYGWIN__)
    if (env_nonempty_to_string("LOCALAPPDATA", out)) {
        return join_child(out, "caudio");
    }
#endif
    if (env_nonempty_to_string("XDG_CONFIG_HOME", out)) {
        return join_child(out, "caudio");
    }
    if (env_nonempty_to_string("HOME", out) ||
        env_nonempty_to_string("USERPROFILE", out)) {
        ca_status status = join_child(out, ".config");
        if (status == CA_OK) {
            status = join_child(out, "caudio");
        }
        return status;
    }
    ca_status status = temp_dir_to_string(out);
    if (status != CA_OK) {
        status = text_assign(out, "/tmp");
    }
    if (status != CA_OK) {
        return status;
    }
    return join_child(out, "caudio");
}

static ca_status socket_base(cx_string *out) {
#if defined(_WIN32) || defined(__CYGWIN__)
    if (env_nonempty_to_string("LOCALAPPDATA", out)) {
        return join_child(out, "caudio");
    }
#endif
    if (env_nonempty_to_string("XDG_RUNTIME_DIR", out)) {
        return join_child(out, "caudio");
    }
    if (env_nonempty_to_string("XDG_DATA_HOME", out)) {
        return join_child(out, "caudio");
    }
    if (env_nonempty_to_string("HOME", out) ||
        env_nonempty_to_string("USERPROFILE", out)) {
        ca_status status = join_child(out, ".local");
        if (status == CA_OK) {
            status = join_child(out, "share");
        }
        if (status == CA_OK) {
            status = join_child(out, "caudio");
        }
        return status;
    }
    ca_status status = temp_dir_to_string(out);
    if (status != CA_OK) {
        status = text_assign(out, "/tmp");
    }
    if (status != CA_OK) {
        return status;
    }
    return join_child(out, "caudio");
}

/* Begin an output string, ready to be filled by a base builder. */
static ca_status output_begin(cx_string **out, const cx_allocator *alloc) {
    if (!out) {
        return CA_INVALID_ARGUMENT;
    }
    *out = nullptr;
    return ca_from_cx(cx_string_init(out, alloc));
}

static ca_status output_fail(cx_string **out, ca_status status) {
    cx_string_destroy(out);
    return status;
}

/* Split off the parent directory of @p path into @p out. */
static ca_status parent_path_to_string(const char *path, cx_string *out) {
    const char *last = nullptr;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            last = p;
        }
    }
    if (last == nullptr) {
        cx_string_clear(out);
        return CA_OK;
    }

    size_t length = (size_t)(last - path);
    if (length == 0) {
        /* Absolute root: "/" or "\". */
        return text_assign(out, "/");
    }
    if (length == 2 && path[1] == ':') {
        /* Windows drive root: "C:" -> "C:/". */
        length = 3;
    }
    return ca_from_cx(
        cx_string_assign_view(out, cx_string_view_from_parts(path, length)));
}

static ca_status mkdirs(const char *path, ca_error *err) {
    if (path == nullptr || path[0] == '\0') {
        return CA_OK;
    }
    return ca_fs_mkdirs(path, err);
}

/* ------------------------------------------------------------------------- */
/* RuntimePaths                                                              */
/* ------------------------------------------------------------------------- */

static ca_status paths_str_copy(cx_string **dst, const cx_string *src,
                                const cx_allocator *alloc) {
    *dst = nullptr;
    if (src == nullptr) {
        return CA_OK;
    }
    return ca_from_cx(cx_string_from_view(dst, cx_string_view_of(src), alloc));
}

ca_status ca_runtime_paths_init(ca_runtime_paths *paths,
                                const cx_allocator *alloc) {
    (void)alloc;
    if (!paths) {
        return CA_INVALID_ARGUMENT;
    }
    memset(paths, 0, sizeof *paths);
    return CA_OK;
}

ca_status ca_runtime_paths_copy(ca_runtime_paths *dst,
                                const ca_runtime_paths *src,
                                const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);

    ca_status status = paths_str_copy(&dst->database, src->database, alloc);
    if (status == CA_OK) {
        status = paths_str_copy(&dst->configuration, src->configuration, alloc);
    }
    if (status == CA_OK) {
        status = paths_str_copy(&dst->logs, src->logs, alloc);
    }
    if (status == CA_OK) {
        status = paths_str_copy(&dst->cache, src->cache, alloc);
    }
    if (status == CA_OK) {
        status = paths_str_copy(&dst->imported_media, src->imported_media,
                                alloc);
    }
    if (status == CA_OK) {
        status = paths_str_copy(&dst->socket, src->socket, alloc);
    }
    if (status != CA_OK) {
        ca_runtime_paths_destroy(dst);
    }
    return status;
}

void ca_runtime_paths_move(ca_runtime_paths *dst, ca_runtime_paths *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_runtime_paths_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_runtime_paths_destroy(ca_runtime_paths *paths) {
    if (!paths) {
        return;
    }
    cx_string_destroy(&paths->database);
    cx_string_destroy(&paths->configuration);
    cx_string_destroy(&paths->logs);
    cx_string_destroy(&paths->cache);
    cx_string_destroy(&paths->imported_media);
    cx_string_destroy(&paths->socket);
    memset(paths, 0, sizeof *paths);
}

static bool path_is_empty(const cx_string *str) {
    return str == nullptr || cx_string_length(str) == 0;
}

bool ca_runtime_paths_empty(const ca_runtime_paths *paths) {
    if (!paths) {
        return true;
    }
    return path_is_empty(paths->database) &&
           path_is_empty(paths->configuration) && path_is_empty(paths->logs) &&
           path_is_empty(paths->cache) &&
           path_is_empty(paths->imported_media) &&
           path_is_empty(paths->socket);
}

static ca_status ensure_path_parent(const cx_string *path, ca_error *err) {
    if (path_is_empty(path)) {
        return CA_OK;
    }

    cx_string *parent = nullptr;
    ca_status status = ca_from_cx(cx_string_init(&parent, nullptr));
    if (status != CA_OK) {
        return status;
    }
    status = parent_path_to_string(cx_string_cstr(path), parent);
    if (status == CA_OK) {
        status = mkdirs(cx_string_cstr(parent), err);
    }
    cx_string_destroy(&parent);
    return status;
}

static ca_status ensure_path_dir(const cx_string *path, ca_error *err) {
    if (path_is_empty(path)) {
        return CA_OK;
    }
    return mkdirs(cx_string_cstr(path), err);
}

ca_status ca_runtime_paths_ensure_directories(const ca_runtime_paths *paths,
                                              ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!paths) {
        return CA_INVALID_ARGUMENT;
    }

    ca_status status = ensure_path_parent(paths->database, err);
    if (status == CA_OK) {
        status = ensure_path_parent(paths->configuration, err);
    }
    if (status == CA_OK) {
        status = ensure_path_dir(paths->logs, err);
    }
    if (status == CA_OK) {
        status = ensure_path_dir(paths->cache, err);
    }
    if (status == CA_OK) {
        status = ensure_path_dir(paths->imported_media, err);
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/* Default paths                                                             */
/* ------------------------------------------------------------------------- */

ca_status ca_paths_default_db_path(cx_string **out, const cx_allocator *alloc,
                                   ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    ca_status status = output_begin(out, alloc);
    if (status != CA_OK) {
        return status;
    }
    status = data_base(*out);
    if (status == CA_OK) {
        status = join_child(*out, "library.db");
    }
    if (status != CA_OK) {
        return output_fail(out, status);
    }
    return CA_OK;
}

ca_status ca_paths_default_config_path(cx_string **out,
                                       const cx_allocator *alloc,
                                       ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    ca_status status = output_begin(out, alloc);
    if (status != CA_OK) {
        return status;
    }
    status = config_base(*out);
    if (status == CA_OK) {
        status = join_child(*out, "config.json");
    }
    if (status != CA_OK) {
        return output_fail(out, status);
    }
    return CA_OK;
}

ca_status ca_paths_legacy_config_path(cx_string **out,
                                      const cx_allocator *alloc) {
    ca_status status = output_begin(out, alloc);
    if (status != CA_OK) {
        return status;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    if (env_nonempty_to_string("USERPROFILE", *out)) {
        status = join_child(*out, ".config");
        if (status == CA_OK) {
            status = join_child(*out, "caudio");
        }
        if (status == CA_OK) {
            status = join_child(*out, "config.json");
        }
        if (status != CA_OK) {
            return output_fail(out, status);
        }
        return CA_OK;
    }
#endif

    /* POSIX and Windows without USERPROFILE: no legacy path. */
    return CA_OK;
}

/* ------------------------------------------------------------------------- */
/* Derived paths                                                             */
/* ------------------------------------------------------------------------- */

/*
 * FNV-1a with the standard 64-bit offset basis. Mirrors
 * ca_path_identity_normalize: separators become '/' and, on Windows, ASCII
 * letters fold to lower case.
 */
static unsigned char identity_byte(unsigned char c) {
    if (c == '\\') {
        return '/';
    }
#if defined(_WIN32) || defined(__CYGWIN__)
    if (c >= 'A' && c <= 'Z') {
        return (unsigned char)(c - 'A' + 'a');
    }
#endif
    return c;
}

void ca_paths_hex8_for_db(const char *db_path, char out[9]) {
    if (out == nullptr) {
        return;
    }
    uint64_t hash = UINT64_C(14695981039346656037);
    const unsigned char *p =
        (const unsigned char *)(db_path ? db_path : "");
    for (; *p != '\0'; ++p) {
        hash ^= identity_byte(*p);
        hash *= UINT64_C(1099511628211);
    }

    uint32_t folded = (uint32_t)(hash & UINT64_C(0xFFFFFFFF));
    folded ^= (uint32_t)((hash >> 32) & UINT64_C(0xFFFFFFFF));
    (void)snprintf(out, 9, "%08x", (unsigned)folded);
}

static ca_status derived_path(const char *db_path, const char *suffix,
                              cx_string **out, const cx_allocator *alloc,
                              ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    ca_status status = output_begin(out, alloc);
    if (status != CA_OK) {
        return status;
    }

    status = socket_base(*out);
    if (status != CA_OK) {
        return output_fail(out, status);
    }

    char hex[9];
    ca_paths_hex8_for_db(db_path, hex);

    char name[32];
    int written = snprintf(name, sizeof name, "caudio-%s%s", hex, suffix);
    if (written < 0 || (size_t)written >= sizeof name) {
        return output_fail(out, CA_OVERFLOW);
    }
    status = join_child(*out, name);
    if (status != CA_OK) {
        return output_fail(out, status);
    }
    return CA_OK;
}

ca_status ca_paths_socket_for(const char *db_path, cx_string **out,
                              const cx_allocator *alloc, ca_error *err) {
    char hex[9];
    ca_paths_hex8_for_db(db_path, hex);

    if (err) {
        ca_error_init(err);
    }
    ca_status status = output_begin(out, alloc);
    if (status != CA_OK) {
        return status;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    status = text_assign(*out, "\\\\.\\pipe\\caudio-");
    if (status == CA_OK) {
        status = text_append(*out, hex);
    }
#else
    status = socket_base(*out);
    if (status == CA_OK) {
        char name[32];
        int written = snprintf(name, sizeof name, "caudio-%s.sock", hex);
        if (written < 0 || (size_t)written >= sizeof name) {
            status = CA_OVERFLOW;
        } else {
            status = join_child(*out, name);
        }
    }
#endif

    if (status != CA_OK) {
        return output_fail(out, status);
    }
    return CA_OK;
}

ca_status ca_paths_pid_for(const char *db_path, cx_string **out,
                           const cx_allocator *alloc, ca_error *err) {
    return derived_path(db_path, ".pid", out, alloc, err);
}

ca_status ca_paths_lock_for(const char *db_path, cx_string **out,
                            const cx_allocator *alloc, ca_error *err) {
    return derived_path(db_path, ".lock", out, alloc, err);
}

ca_status ca_paths_log_for(const char *db_path, cx_string **out,
                           const cx_allocator *alloc, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    ca_status status = output_begin(out, alloc);
    if (status != CA_OK) {
        return status;
    }

    status = parent_path_to_string(db_path ? db_path : "", *out);
    if (status == CA_OK && cx_string_length(*out) == 0) {
        status = temp_dir_to_string(*out);
        if (status != CA_OK) {
            status = text_assign(*out, "/tmp");
        }
    }
    if (status == CA_OK) {
        status = join_child(*out, "caudio.log");
    }
    if (status != CA_OK) {
        return output_fail(out, status);
    }
    return CA_OK;
}
