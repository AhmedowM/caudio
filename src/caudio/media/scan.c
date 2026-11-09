#include <caudio/media/scan.h>
#include <caudio/media/audio_ext.h>
#include <caudio/media/fingerprint.h>

#include <caudio/runtime/error.h>

#include <cstdx/allocator.h>
#include <cstdx/string.h>
#include <cstdx/vector.h>

#include <caudio/types/record.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#endif

/* Forward-declared in src/caudio/types/internal.h; defined in record.c */
extern const cx_vector_ops ca_impl_track_vector_ops;

/* ---------------------------------------------------------- */
/* Directory iteration (cross-platform)                       */
/* ---------------------------------------------------------- */

#if defined(_WIN32)
struct dir_scan {
    HANDLE hFind;
    WIN32_FIND_DATAW data;
    bool has_current;
    char path_buf[4096];
    char name_buf[4096];
};

static bool scan_open(struct dir_scan* scan, const char* dir) {
    snprintf(scan->path_buf, sizeof(scan->path_buf), "%s\\*", dir);
    int wlen = MultiByteToWideChar(CP_UTF8, 0, scan->path_buf, -1, nullptr, 0);
    if (wlen <= 0) return false;
    const cx_allocator* alloc = cx_allocator_default();
    WCHAR* wbuf = (WCHAR*)cx_allocator_alloc(alloc, (size_t)wlen * sizeof(WCHAR), alignof(WCHAR));
    if (!wbuf) return false;
    MultiByteToWideChar(CP_UTF8, 0, scan->path_buf, -1, wbuf, wlen);
    scan->hFind = FindFirstFileW(wbuf, &scan->data);
    cx_allocator_free(alloc, wbuf, (size_t)wlen * sizeof(WCHAR), alignof(WCHAR));
    scan->has_current = scan->hFind != INVALID_HANDLE_VALUE;
    return scan->has_current;
}

static bool scan_next(struct dir_scan* scan) {
    if (!scan->has_current) return false;
    BOOL r = FindNextFileW(scan->hFind, &scan->data);
    if (!r) {
        FindClose(scan->hFind);
        scan->hFind = INVALID_HANDLE_VALUE;
        scan->has_current = false;
        return false;
    }
    return true;
}

static const char* scan_name(struct dir_scan* scan) {
    int len = WideCharToMultiByte(CP_UTF8, 0, scan->data.cFileName, -1,
                                  scan->name_buf, (int)sizeof(scan->name_buf),
                                  nullptr, nullptr);
    if (len <= 0) return nullptr;
    scan->name_buf[len - 1] = '\0';
    return scan->name_buf;
}

static void scan_close(struct dir_scan* scan) {
    if (scan->hFind != INVALID_HANDLE_VALUE) {
        FindClose(scan->hFind);
        scan->hFind = INVALID_HANDLE_VALUE;
    }
}
#endif

/* ---------------------------------------------------------- */
/* Status conversion                                          */
/* ---------------------------------------------------------- */

static ca_status ca_from_cx(cx_status status) {
    switch (status) {
    case CX_OK:             return CA_OK;
    case CX_OUT_OF_MEMORY:  return CA_OUT_OF_MEMORY;
    case CX_INVALID_ARGUMENT: return CA_INVALID_ARGUMENT;
    case CX_OVERFLOW:       return CA_OVERFLOW;
    default:                return CA_INVALID_ARGUMENT;
    }
}

/* ---------------------------------------------------------- */
/* Track construction                                         */
/* ---------------------------------------------------------- */

static ca_status make_track(const char* path,
                            ca_media_scan_mode_t mode,
                            ca_track* out,
                            ca_error* err) {
    if (!path || !out) return CA_INVALID_ARGUMENT;
    ca_error_init(err);
    memset(out, 0, sizeof(*out));
    out->library_id = 1;
    out->source.kind = CA_SOURCE_PATH;

    const cx_allocator* alloc = cx_allocator_default();
    cx_string** slots[] = {
        &out->path, &out->title, &out->artist,
        &out->album, &out->album_artist, &out->genre,
        &out->cover_art_path,
    };
    ca_status st = CA_OK;
    for (size_t i = 0; i < sizeof slots / sizeof slots[0]; ++i) {
        st = ca_from_cx(cx_string_init(slots[i], alloc));
        if (st != CA_OK) {
            ca_track_destroy(out);
            return st;
        }
    }

    st = ca_from_cx(cx_string_assign_view(
        out->path, cx_string_view_from_cstr(path)));
    if (st != CA_OK) {
        ca_track_destroy(out);
        return st;
    }

    struct stat stbuf;
    if (stat(path, &stbuf) == 0) {
        out->size = (int64_t)stbuf.st_size;
        out->mtime = (int64_t)stbuf.st_mtime;
    }

    if (mode == CA_MEDIA_SCAN_SAMPLED || mode == CA_MEDIA_SCAN_FULL) {
        st = ca_media_fingerprint_compute(path, out->fingerprint, err);
        if (st != CA_OK) {
            ca_track_destroy(out);
            return st;
        }
    }
    return CA_OK;
}

/* ---------------------------------------------------------- */
/* Callback wrapper for vector collection                     */
/* ---------------------------------------------------------- */

struct scan_ctx {
    cx_vector* vec;
    ca_error* err;
    ca_status last_st;
};

static bool scan_track_cb(const ca_track* track, void* user_data) {
    struct scan_ctx* ctx = (struct scan_ctx*)user_data;
    ca_status st = ca_from_cx(cx_vector_push_copy(ctx->vec, track, sizeof(ca_track)));
    if (st != CA_OK) {
        ctx->last_st = st;
        ca_error_set(ctx->err, st, "push_copy failed");
        return false;
    }
    return true;
}

/* ---------------------------------------------------------- */
/* Recursive directory scan                                   */
/* ---------------------------------------------------------- */

static ca_status scan_dir(const char* dir,
                          ca_media_scan_mode_t mode,
                          ca_media_track_cb cb,
                          void* user_data,
                          ca_error* err) {
    struct dir_scan scan;
    if (!scan_open(&scan, dir)) {
        return CA_OK; /* skip unreadable/missing dirs */
    }

    while (scan_next(&scan)) {
        const char* name = scan_name(&scan);
        if (!name || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        size_t dir_len = strlen(dir);
        size_t name_len = strlen(name);
        size_t full_len = dir_len + 1 + name_len + 1;
        const cx_allocator* alloc = cx_allocator_default();
        char* full = (char*)cx_allocator_alloc(alloc, full_len, 1);
        if (!full) {
            scan_close(&scan);
            if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
            return CA_OUT_OF_MEMORY;
        }
        memcpy(full, dir, dir_len);
        full[dir_len] = '/';
        memcpy(full + dir_len + 1, name, name_len + 1);

        struct stat stbuf;
        if (stat(full, &stbuf) == 0 && S_ISDIR(stbuf.st_mode)) {
            ca_status st2 = scan_dir(full, mode, cb, user_data, err);
            cx_allocator_free(alloc, full, full_len, 1);
            if (st2 != CA_OK) {
                scan_close(&scan);
                return st2;
            }
        } else if (ca_media_has_audio_ext(full)) {
            ca_track track;
            ca_status st2 = make_track(full, mode, &track, err);
            if (st2 != CA_OK) {
                cx_allocator_free(alloc, full, full_len, 1);
                scan_close(&scan);
                ca_track_destroy(&track);
                return st2;
            }
            bool cont = cb(&track, user_data);
            ca_track_destroy(&track);
            cx_allocator_free(alloc, full, full_len, 1);
            if (!cont) {
                scan_close(&scan);
                return CA_OK;
            }
        } else {
            cx_allocator_free(alloc, full, full_len, 1);
        }
    }

    scan_close(&scan);
    return CA_OK;
}

ca_status ca_media_scan_tracks(const char* root,
                               ca_media_scan_mode_t mode,
                               ca_media_track_cb cb,
                               void* user_data,
                               ca_error* err) {
    if (err) ca_error_init(err);
    if (!root || !cb) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        return CA_INVALID_ARGUMENT;
    }

    struct stat stbuf;
    if (stat(root, &stbuf) != 0 || !S_ISDIR(stbuf.st_mode)) {
        if (err) ca_error_set(err, CA_NOT_FOUND, "root is not a directory");
        return CA_NOT_FOUND;
    }

    return scan_dir(root, mode, cb, user_data, err);
}

ca_status ca_media_scan_tracks_to_vector(const char* root,
                                         ca_media_scan_mode_t mode,
                                         cx_vector** out,
                                         const cx_allocator* alloc,
                                         ca_error* err) {
    if (err) ca_error_init(err);
    if (!out || !alloc) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        return CA_INVALID_ARGUMENT;
    }
    *out = nullptr;

    ca_status st = ca_from_cx(cx_vector_init(out, sizeof(ca_track), alignof(ca_track),
                                              &ca_impl_track_vector_ops, alloc));
    if (st != CA_OK) return st;

    struct scan_ctx ctx;
    ctx.vec = *out;
    ctx.err = err;
    ctx.last_st = CA_OK;

    st = scan_dir(root, mode, scan_track_cb, &ctx, err);
    if (st != CA_OK || ctx.last_st != CA_OK) {
        cx_vector_destroy(out);
        *out = nullptr;
        if (ctx.last_st != CA_OK) {
            ca_error_set(err, ctx.last_st, "track push failed");
        }
        return ctx.last_st != CA_OK ? ctx.last_st : st;
    }
    return CA_OK;
}
