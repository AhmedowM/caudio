#include <caudio/runtime/json.h>

#include <cstdx/allocator.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <yyjson.h>

/**
 * @file caudio/runtime/json.c
 * @brief ca_json wrapper over yyjson.
 *
 * ca_json exposes yyjson through caudio's error model and allocator contract.
 * All JSON memory is allocated through the supplied cx_allocator (or the
 * default allocator when NULL).
 */

/**
 * @brief Allocator state handed to yyjson through yyjson_alc.
 *
 * yyjson's free callback only receives the pointer, while cx_allocator_free
 * also requires the allocation size. Every block therefore carries a small
 * header that records its size. The header is sized and aligned as
 * max_align_t, so the pointer returned to yyjson stays suitably aligned.
 */
typedef struct ca_json_alloc_ctx {
    const cx_allocator *alloc;
} ca_json_alloc_ctx;

typedef union ca_json_block_hdr {
    struct {
        size_t size;
    } info;
    max_align_t align;
} ca_json_block_hdr;

struct ca_json {
    yyjson_doc *doc;

    /* Copy of the caller's vtable. The copy, not the caller's possibly
     * stack-allocated object, is what yyjson refers to for the document's
     * lifetime. */
    cx_allocator alloc;

    /* yyjson_doc stores the yyjson_alc by value, including this context
     * pointer, and uses it in yyjson_doc_free(). It must therefore outlive
     * the document. */
    ca_json_alloc_ctx alc_ctx;
};

static void *ca_json_malloc(void *ctx, size_t size) {
    ca_json_alloc_ctx *c = (ca_json_alloc_ctx *)ctx;

    /* yyjson may ask for zero bytes; a valid, freeable pointer is simpler
     * than special-casing the result. */
    if (size == 0) {
        size = 1;
    }

    if (size > SIZE_MAX - sizeof(ca_json_block_hdr)) {
        return NULL;
    }

    ca_json_block_hdr *hdr = (ca_json_block_hdr *)cx_allocator_alloc(
        c->alloc, sizeof(ca_json_block_hdr) + size, 0);
    if (!hdr) {
        return NULL;
    }

    hdr->info.size = size;
    return hdr + 1;
}

static void *ca_json_realloc(void *ctx, void *ptr, size_t old_size,
                             size_t new_size) {
    (void)old_size;

    if (!ptr) {
        return ca_json_malloc(ctx, new_size);
    }

    if (new_size == 0) {
        ca_json_alloc_ctx *c = (ca_json_alloc_ctx *)ctx;
        ca_json_block_hdr *hdr = (ca_json_block_hdr *)ptr - 1;
        cx_allocator_free(c->alloc, hdr,
                          sizeof(ca_json_block_hdr) + hdr->info.size, 0);
        return NULL;
    }

    if (new_size > SIZE_MAX - sizeof(ca_json_block_hdr)) {
        return NULL;
    }

    ca_json_alloc_ctx *c = (ca_json_alloc_ctx *)ctx;
    ca_json_block_hdr *hdr = (ca_json_block_hdr *)ptr - 1;
    size_t old_block = sizeof(ca_json_block_hdr) + hdr->info.size;
    size_t new_block = sizeof(ca_json_block_hdr) + new_size;

    ca_json_block_hdr *resized = (ca_json_block_hdr *)cx_allocator_realloc(
        c->alloc, hdr, old_block, new_block, 0);
    if (!resized) {
        return NULL;
    }

    resized->info.size = new_size;
    return resized + 1;
}

static void ca_json_free(void *ctx, void *ptr) {
    if (!ptr) {
        return;
    }

    ca_json_alloc_ctx *c = (ca_json_alloc_ctx *)ctx;
    ca_json_block_hdr *hdr = (ca_json_block_hdr *)ptr - 1;
    cx_allocator_free(c->alloc, hdr,
                      sizeof(ca_json_block_hdr) + hdr->info.size, 0);
}

static yyjson_alc ca_json_make_alc(ca_json_alloc_ctx *ctx) {
    return (yyjson_alc){
        .malloc = ca_json_malloc,
        .realloc = ca_json_realloc,
        .free = ca_json_free,
        .ctx = ctx,
    };
}

ca_status ca_json_parse(ca_json **out, const char *text, size_t len,
                        const cx_allocator *alloc, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (!out) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is NULL");
        }
        return CA_INVALID_ARGUMENT;
    }

    *out = NULL;

    if (!text) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "text is NULL");
        }
        return CA_INVALID_ARGUMENT;
    }

    if (len == 0) {
        len = strlen(text);
    }

    const cx_allocator *a = alloc ? alloc : cx_allocator_default();

    ca_json *json = (ca_json *)cx_allocator_alloc(a, sizeof(ca_json),
                                                  alignof(ca_json));
    if (!json) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate JSON handle");
        }
        return CA_OUT_OF_MEMORY;
    }

    json->doc = NULL;
    json->alloc = *a;
    json->alc_ctx.alloc = &json->alloc;

    yyjson_alc alc = ca_json_make_alc(&json->alc_ctx);
    yyjson_read_err rerr = {0};
    yyjson_doc *doc = yyjson_read_opts((char *)text, len, 0, &alc, &rerr);

    if (!doc) {
        ca_status status = rerr.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION
                               ? CA_OUT_OF_MEMORY
                               : CA_INVALID_ARGUMENT;
        if (err) {
            ca_error_set(err, status, "JSON parse error: %s at byte %lu",
                         rerr.msg ? rerr.msg : "unknown",
                         (unsigned long)rerr.pos);
        }
        cx_allocator_free(&json->alloc, json, sizeof(ca_json),
                          alignof(ca_json));
        return status;
    }

    json->doc = doc;
    *out = json;
    return CA_OK;
}

ca_status ca_json_parse_file(ca_json **out, const char *path,
                             const cx_allocator *alloc, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (!out) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is NULL");
        }
        return CA_INVALID_ARGUMENT;
    }

    *out = NULL;

    if (!path) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "path is NULL");
        }
        return CA_INVALID_ARGUMENT;
    }

    const cx_allocator *a = alloc ? alloc : cx_allocator_default();

    ca_json *json = (ca_json *)cx_allocator_alloc(a, sizeof(ca_json),
                                                  alignof(ca_json));
    if (!json) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate JSON handle");
        }
        return CA_OUT_OF_MEMORY;
    }

    json->doc = NULL;
    json->alloc = *a;
    json->alc_ctx.alloc = &json->alloc;

    yyjson_alc alc = ca_json_make_alc(&json->alc_ctx);
    yyjson_read_err rerr = {0};
    yyjson_doc *doc = yyjson_read_file(path, 0, &alc, &rerr);

    if (!doc) {
        /* yyjson exposes error codes as const objects rather than enum
         * constants, so they cannot be used as switch labels. */
        ca_status status = CA_INVALID_ARGUMENT;

        if (rerr.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION) {
            status = CA_OUT_OF_MEMORY;
        } else if (rerr.code == YYJSON_READ_ERROR_FILE_OPEN ||
                   rerr.code == YYJSON_READ_ERROR_FILE_READ) {
            status = CA_IO_ERROR;
        }

        if (err) {
            ca_error_set(err, status, "JSON file error: %s",
                         rerr.msg ? rerr.msg : "unknown");
        }
        cx_allocator_free(&json->alloc, json, sizeof(ca_json),
                          alignof(ca_json));
        return status;
    }

    json->doc = doc;
    *out = json;
    return CA_OK;
}

void ca_json_destroy(ca_json **out) {
    if (!out || !*out) {
        return;
    }

    ca_json *json = *out;
    *out = NULL;

    /* yyjson_doc_free() calls back through json->alc_ctx, so the handle must
     * still be alive here. */
    yyjson_doc_free(json->doc);
    cx_allocator_free(&json->alloc, json, sizeof(ca_json), alignof(ca_json));
}

ca_json_type ca_json_type_of(const ca_json_val *val) {
    if (!val) {
        return CA_JSON_NULL;
    }

    const yyjson_val *yv = (const yyjson_val *)val;
    yyjson_type t = yyjson_get_type(yv);
    yyjson_subtype s = yyjson_get_subtype(yv);

    switch (t) {
    case YYJSON_TYPE_NULL:
        return CA_JSON_NULL;
    case YYJSON_TYPE_BOOL:
        return s == YYJSON_SUBTYPE_TRUE ? CA_JSON_TRUE : CA_JSON_FALSE;
    case YYJSON_TYPE_NUM:
        switch (s) {
        case YYJSON_SUBTYPE_UINT:
            return CA_JSON_UINT;
        case YYJSON_SUBTYPE_SINT:
            return CA_JSON_INT;
        case YYJSON_SUBTYPE_REAL:
            return CA_JSON_DOUBLE;
        default:
            return CA_JSON_NULL;
        }
    case YYJSON_TYPE_STR:
        return CA_JSON_STRING;
    case YYJSON_TYPE_ARR:
        return CA_JSON_ARRAY;
    case YYJSON_TYPE_OBJ:
        return CA_JSON_OBJECT;
    case YYJSON_TYPE_RAW:
    default:
        return CA_JSON_NULL;
    }
}

const char *ca_json_get_string(const ca_json_val *val, size_t *len) {
    if (!val) {
        if (len) {
            *len = 0;
        }
        return NULL;
    }

    const yyjson_val *yv = (const yyjson_val *)val;
    if (yyjson_get_type(yv) != YYJSON_TYPE_STR) {
        if (len) {
            *len = 0;
        }
        return NULL;
    }

    const char *str = yyjson_get_str(yv);
    if (len) {
        *len = yyjson_get_len(yv);
    }
    return str;
}

int64_t ca_json_get_int(const ca_json_val *val) {
    if (!val) {
        return 0;
    }

    const yyjson_val *yv = (const yyjson_val *)val;
    if (yyjson_get_type(yv) != YYJSON_TYPE_NUM ||
        yyjson_get_subtype(yv) != YYJSON_SUBTYPE_SINT) {
        return 0;
    }

    return yyjson_get_sint(yv);
}

uint64_t ca_json_get_uint(const ca_json_val *val) {
    if (!val) {
        return 0;
    }

    const yyjson_val *yv = (const yyjson_val *)val;
    if (yyjson_get_type(yv) != YYJSON_TYPE_NUM ||
        yyjson_get_subtype(yv) != YYJSON_SUBTYPE_UINT) {
        return 0;
    }

    return yyjson_get_uint(yv);
}

double ca_json_get_double(const ca_json_val *val) {
    if (!val) {
        return 0.0;
    }

    const yyjson_val *yv = (const yyjson_val *)val;
    if (yyjson_get_type(yv) != YYJSON_TYPE_NUM ||
        yyjson_get_subtype(yv) != YYJSON_SUBTYPE_REAL) {
        return 0.0;
    }

    return yyjson_get_real(yv);
}

size_t ca_json_array_size(const ca_json_val *val) {
    if (!val) {
        return 0;
    }

    const yyjson_val *yv = (const yyjson_val *)val;
    if (yyjson_get_type(yv) != YYJSON_TYPE_ARR) {
        return 0;
    }

    return yyjson_arr_size(yv);
}

const ca_json_val *ca_json_array_get(const ca_json_val *val, size_t idx) {
    if (!val) {
        return NULL;
    }

    const yyjson_val *yv = (const yyjson_val *)val;
    if (yyjson_get_type(yv) != YYJSON_TYPE_ARR) {
        return NULL;
    }

    const yyjson_val *item = yyjson_arr_get(yv, idx);
    return (const ca_json_val *)item;
}

size_t ca_json_object_size(const ca_json_val *val) {
    if (!val) {
        return 0;
    }

    const yyjson_val *yv = (const yyjson_val *)val;
    if (yyjson_get_type(yv) != YYJSON_TYPE_OBJ) {
        return 0;
    }

    return yyjson_obj_size(yv);
}

const ca_json_val *ca_json_object_get(const ca_json_val *val, const char *key) {
    if (!val || !key) {
        return NULL;
    }

    const yyjson_val *yv = (const yyjson_val *)val;
    if (yyjson_get_type(yv) != YYJSON_TYPE_OBJ) {
        return NULL;
    }

    const yyjson_val *item = yyjson_obj_get(yv, key);
    return (const ca_json_val *)item;
}

const ca_json_val *ca_json_root(const ca_json *json) {
    if (!json) {
        return NULL;
    }

    return (const ca_json_val *)yyjson_doc_get_root(json->doc);
}

char *ca_json_serialize(const ca_json *json, int pretty, size_t *len,
                        const cx_allocator *alloc, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (len) {
        *len = 0;
    }

    if (!json) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "json is NULL");
        }
        return NULL;
    }

    const cx_allocator *a = alloc ? alloc : cx_allocator_default();

    /* The writer may allocate a buffer larger than the emitted document. Use
     * the tracked adapter for it, then copy into an exact-size block so the
     * caller can free the result with the size it was given. */
    ca_json_alloc_ctx ctx = {.alloc = a};
    yyjson_alc yalc = ca_json_make_alc(&ctx);

    yyjson_write_flag flg = pretty ? YYJSON_WRITE_PRETTY : YYJSON_WRITE_NOFLAG;
    yyjson_write_err werr = {0};
    size_t written = 0;
    char *tmp = yyjson_write_opts(json->doc, flg, &yalc, &written, &werr);

    if (!tmp) {
        ca_status status = werr.code == YYJSON_WRITE_ERROR_MEMORY_ALLOCATION
                               ? CA_OUT_OF_MEMORY
                               : CA_INVALID_ARGUMENT;
        if (err) {
            ca_error_set(err, status, "failed to serialize JSON: %s",
                         werr.msg ? werr.msg : "unknown");
        }
        return NULL;
    }

    char *result = (char *)cx_allocator_alloc(a, written + 1, 0);
    if (!result) {
        ca_json_free(&ctx, tmp);
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY,
                         "failed to allocate JSON output");
        }
        return NULL;
    }

    memcpy(result, tmp, written);
    result[written] = '\0';
    ca_json_free(&ctx, tmp);

    if (len) {
        *len = written;
    }
    return result;
}
