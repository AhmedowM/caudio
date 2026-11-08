#include <caudio/runtime/json.h>

#include <caudio/runtime/fs.h>

#include <cstdx/allocator.h>
#include <cstdx/string.h>
#include <cstdx/string_builder.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

/* yyjson fails when the container depth reaches the configured limit, so the
 * public cap permits one fewer level. Keep the two in sync: this fails the
 * build rather than silently accepting deeper input. */
#if YYJSON_READER_DEPTH_LIMIT != (CA_JSON_MAX_DEPTH + 1)
    #error "YYJSON_READER_DEPTH_LIMIT must equal CA_JSON_MAX_DEPTH + 1"
#endif

/**
 * @file caudio/runtime/json.c
 * @brief ca_json wrapper over yyjson.
 *
 * ca_json exposes yyjson through caudio's error model and allocator contract.
 * All JSON memory is allocated through the supplied cx_allocator (or the
 * default allocator when nullptr).
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
        return nullptr;
    }

    ca_json_block_hdr *hdr = (ca_json_block_hdr *)cx_allocator_alloc(
        c->alloc, sizeof(ca_json_block_hdr) + size, 0);
    if (!hdr) {
        return nullptr;
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
        return nullptr;
    }

    if (new_size > SIZE_MAX - sizeof(ca_json_block_hdr)) {
        return nullptr;
    }

    ca_json_alloc_ctx *c = (ca_json_alloc_ctx *)ctx;
    ca_json_block_hdr *hdr = (ca_json_block_hdr *)ptr - 1;
    size_t old_block = sizeof(ca_json_block_hdr) + hdr->info.size;
    size_t new_block = sizeof(ca_json_block_hdr) + new_size;

    ca_json_block_hdr *resized = (ca_json_block_hdr *)cx_allocator_realloc(
        c->alloc, hdr, old_block, new_block, 0);
    if (!resized) {
        return nullptr;
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

/**
 * @brief Walk a parsed document and enforce the public depth/container caps.
 *
 * yyjson is built with a parse-time depth limit, so this traversal is bounded.
 * It still runs unconditionally: the public limits are part of the ca_json
 * contract and must not depend on a particular yyjson build configuration.
 */
static ca_status json_validate_limits(const yyjson_val *val, size_t depth,
                                      ca_error *err) {
    if (!val) {
        return CA_OK;
    }

    yyjson_type type = yyjson_get_type(val);
    if (type != YYJSON_TYPE_ARR && type != YYJSON_TYPE_OBJ) {
        return CA_OK;
    }

    size_t next_depth = depth + 1;
    if (next_depth > (size_t)CA_JSON_MAX_DEPTH) {
        if (err) {
            ca_error_set(err, CA_OVERFLOW,
                         "JSON nesting exceeds %d levels", CA_JSON_MAX_DEPTH);
        }
        return CA_OVERFLOW;
    }

    size_t count = type == YYJSON_TYPE_ARR ? yyjson_arr_size(val)
                                           : yyjson_obj_size(val);
    if (count > (size_t)CA_JSON_MAX_CONTAINER) {
        if (err) {
            ca_error_set(err, CA_OVERFLOW,
                         "JSON container has %zu entries; limit is %d", count,
                         CA_JSON_MAX_CONTAINER);
        }
        return CA_OVERFLOW;
    }

    if (type == YYJSON_TYPE_ARR) {
        yyjson_arr_iter iter = yyjson_arr_iter_with(val);
        const yyjson_val *child = nullptr;
        while ((child = yyjson_arr_iter_next(&iter)) != nullptr) {
            ca_status status = json_validate_limits(child, next_depth, err);
            if (status != CA_OK) {
                return status;
            }
        }
    } else {
        yyjson_obj_iter iter = yyjson_obj_iter_with(val);
        yyjson_val *key = nullptr;
        while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
            const yyjson_val *child = yyjson_obj_iter_get_val(key);
            ca_status status = json_validate_limits(child, next_depth, err);
            if (status != CA_OK) {
                return status;
            }
        }
    }

    return CA_OK;
}

ca_status ca_json_parse(ca_json **out, const char *text, size_t len,
                        const cx_allocator *alloc, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (!out) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    *out = nullptr;

    if (!text) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "text is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    if (len == 0) {
        len = strlen(text);
    }

    if (len > CA_JSON_MAX_BYTES) {
        if (err) {
            ca_error_set(err, CA_OVERFLOW,
                         "JSON input is %zu bytes; limit is %zu", len,
                         CA_JSON_MAX_BYTES);
        }
        return CA_OVERFLOW;
    }

    const cx_allocator *a = alloc ? alloc : cx_allocator_default();

    ca_json *json = (ca_json *)cx_allocator_alloc(a, sizeof(ca_json),
                                                  alignof(ca_json));
    if (!json) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY,
                         "failed to allocate JSON handle");
        }
        return CA_OUT_OF_MEMORY;
    }

    json->doc = nullptr;
    json->alloc = *a;
    json->alc_ctx.alloc = &json->alloc;

    yyjson_alc alc = ca_json_make_alc(&json->alc_ctx);
    yyjson_read_err rerr = {0};
    yyjson_doc *doc = yyjson_read_opts((char *)text, len, 0, &alc, &rerr);

    if (!doc) {
        ca_status status = CA_INVALID_ARGUMENT;

        if (rerr.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION) {
            status = CA_OUT_OF_MEMORY;
        } else if (rerr.code == YYJSON_READ_ERROR_DEPTH) {
            status = CA_OVERFLOW;
        }

        if (err) {
            if (status == CA_OVERFLOW) {
                ca_error_set(err, status, "JSON nesting exceeds %d levels",
                             CA_JSON_MAX_DEPTH);
            } else {
                ca_error_set(err, status, "JSON parse error: %s at byte %lu",
                             rerr.msg ? rerr.msg : "unknown",
                             (unsigned long)rerr.pos);
            }
        }
        cx_allocator_free(&json->alloc, json, sizeof(ca_json),
                          alignof(ca_json));
        return status;
    }

    ca_status limit_status =
        json_validate_limits(yyjson_doc_get_root(doc), 0, err);
    if (limit_status != CA_OK) {
        yyjson_doc_free(doc);
        cx_allocator_free(&json->alloc, json, sizeof(ca_json),
                          alignof(ca_json));
        return limit_status;
    }

    json->doc = doc;
    *out = json;
    return CA_OK;
}

/**
 * @brief Read a whole file into a buffer, enforcing CA_JSON_MAX_BYTES.
 *
 * The returned buffer is allocated with @p alloc, is NUL-terminated, and must
 * be released with cx_allocator_free(alloc, buffer, *out_capacity, 0).
 *
 * @return CA_OK, CA_IO_ERROR, CA_OUT_OF_MEMORY, or CA_OVERFLOW.
 */
static ca_status json_read_all(const char *path, const cx_allocator *alloc,
                               char **out_text, size_t *out_len,
                               size_t *out_capacity, ca_error *err) {
    *out_text = nullptr;
    *out_len = 0;
    *out_capacity = 0;

    ca_file_t file = ca_fs_open(path, CA_FS_READ, err);
    if (file == CA_FS_INVALID) {
        if (err && err->code != CA_OK) {
            return err->code;
        }
        return CA_IO_ERROR;
    }

    /* Read in chunks up to one byte past the cap: that extra byte is enough to
     * distinguish "exactly at the limit" from "over the limit". */
    const size_t max_capacity = CA_JSON_MAX_BYTES + 1;
    size_t capacity = 64 * 1024;
    if (capacity > max_capacity) {
        capacity = max_capacity;
    }

    char *buffer = (char *)cx_allocator_alloc(alloc, capacity, 0);
    if (!buffer) {
        ca_fs_close(file);
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY,
                         "failed to allocate JSON read buffer");
        }
        return CA_OUT_OF_MEMORY;
    }

    size_t total = 0;
    ca_status status = CA_OK;

    for (;;) {
        if (total == capacity) {
            size_t new_capacity = capacity * 2;
            if (new_capacity < capacity || new_capacity > max_capacity) {
                new_capacity = max_capacity;
            }

            char *grown = (char *)cx_allocator_realloc(
                alloc, buffer, capacity, new_capacity, 0);
            if (!grown) {
                status = CA_OUT_OF_MEMORY;
                if (err) {
                    ca_error_set(err, CA_OUT_OF_MEMORY,
                                 "failed to grow JSON read buffer");
                }
                break;
            }

            buffer = grown;
            capacity = new_capacity;
        }

        size_t got = ca_fs_read(file, buffer + total, capacity - total, err);
        if (got == (size_t)-1) {
            status = (err && err->code != CA_OK) ? err->code : CA_IO_ERROR;
            break;
        }

        if (got == 0) {
            break;
        }

        total += got;
        if (total > CA_JSON_MAX_BYTES) {
            status = CA_OVERFLOW;
            if (err) {
                ca_error_set(err, CA_OVERFLOW,
                             "JSON file exceeds %zu bytes",
                             CA_JSON_MAX_BYTES);
            }
            break;
        }
    }

    ca_fs_close(file);

    if (status != CA_OK) {
        cx_allocator_free(alloc, buffer, capacity, 0);
        return status;
    }

    /* capacity > total is guaranteed: growth stops at CA_JSON_MAX_BYTES + 1
     * and any read that pushes total over the cap is rejected above. */
    buffer[total] = '\0';
    *out_text = buffer;
    *out_len = total;
    *out_capacity = capacity;
    return CA_OK;
}

ca_status ca_json_parse_file(ca_json **out, const char *path,
                             const cx_allocator *alloc, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (!out) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    *out = nullptr;

    if (!path || path[0] == '\0') {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "path is nullptr or empty");
        }
        return CA_INVALID_ARGUMENT;
    }

    const cx_allocator *a = alloc ? alloc : cx_allocator_default();

    char *text = nullptr;
    size_t len = 0;
    size_t capacity = 0;
    ca_status status = json_read_all(path, a, &text, &len, &capacity, err);
    if (status != CA_OK) {
        return status;
    }

    status = ca_json_parse(out, text, len, alloc, err);
    cx_allocator_free(a, text, capacity, 0);
    return status;
}

void ca_json_destroy(ca_json **out) {
    if (!out || !*out) {
        return;
    }

    ca_json *json = *out;
    *out = nullptr;

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
        return nullptr;
    }

    const yyjson_val *yv = (const yyjson_val *)val;
    if (yyjson_get_type(yv) != YYJSON_TYPE_STR) {
        if (len) {
            *len = 0;
        }
        return nullptr;
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
        return nullptr;
    }

    const yyjson_val *yv = (const yyjson_val *)val;
    if (yyjson_get_type(yv) != YYJSON_TYPE_ARR) {
        return nullptr;
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
        return nullptr;
    }

    const yyjson_val *yv = (const yyjson_val *)val;
    if (yyjson_get_type(yv) != YYJSON_TYPE_OBJ) {
        return nullptr;
    }

    const yyjson_val *item = yyjson_obj_get(yv, key);
    return (const ca_json_val *)item;
}

const char *ca_json_object_key(const ca_json_val *val, size_t idx,
                               size_t *len) {
    if (len) {
        *len = 0;
    }
    if (!val) {
        return nullptr;
    }

    const yyjson_val *yv = (const yyjson_val *)val;
    if (yyjson_get_type(yv) != YYJSON_TYPE_OBJ) {
        return nullptr;
    }

    yyjson_obj_iter iter;
    if (!yyjson_obj_iter_init(yv, &iter)) {
        return nullptr;
    }
    yyjson_val *key = nullptr;
    for (size_t i = 0; (key = yyjson_obj_iter_next(&iter)) != nullptr; ++i) {
        if (i == idx) {
            if (len) {
                *len = yyjson_get_len(key);
            }
            return yyjson_get_str(key);
        }
    }
    return nullptr;
}

const ca_json_val *ca_json_root(const ca_json *json) {
    if (!json) {
        return nullptr;
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
            ca_error_set(err, CA_INVALID_ARGUMENT, "json is nullptr");
        }
        return nullptr;
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
        return nullptr;
    }

    char *result = (char *)cx_allocator_alloc(a, written + 1, 0);
    if (!result) {
        ca_json_free(&ctx, tmp);
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY,
                         "failed to allocate JSON output");
        }
        return nullptr;
    }

    memcpy(result, tmp, written);
    result[written] = '\0';
    ca_json_free(&ctx, tmp);

    if (len) {
        *len = written;
    }
    return result;
}

/* ------------------------------------------------------------------------- */
/* Incremental writer                                                        */
/* ------------------------------------------------------------------------- */

enum {
    CA_JSON_FRAME_ROOT = 0,
    CA_JSON_FRAME_OBJECT = 1,
    CA_JSON_FRAME_ARRAY = 2
};

typedef struct ca_json_frame {
    uint8_t kind;
    size_t count;
} ca_json_frame;

struct ca_json_writer {
    cx_string_builder *builder;
    cx_allocator alloc;
    ca_json_frame frames[(size_t)CA_JSON_MAX_DEPTH + 1];
    size_t depth;
    bool pretty;
    bool root_written;
};

static ca_status json_from_cx(cx_status status, ca_error *err) {
    switch (status) {
    case CX_OK:
        return CA_OK;
    case CX_OUT_OF_MEMORY:
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "JSON writer is out of memory");
        }
        return CA_OUT_OF_MEMORY;
    case CX_INVALID_ARGUMENT:
        return CA_INVALID_ARGUMENT;
    case CX_OVERFLOW:
        return CA_OVERFLOW;
    default:
        return CA_INVALID_ARGUMENT;
    }
}

static ca_status json_append_cstr(ca_json_writer *writer, const char *text,
                                  ca_error *err) {
    return json_from_cx(cx_string_builder_append_cstr(writer->builder, text),
                        err);
}

static ca_status json_append_char(ca_json_writer *writer, char value,
                                  ca_error *err) {
    return json_from_cx(cx_string_builder_append_char(writer->builder, value),
                        err);
}

static ca_status json_append_indent(ca_json_writer *writer, size_t depth,
                                    ca_error *err) {
    ca_status status = json_append_char(writer, '\n', err);
    if (status != CA_OK) {
        return status;
    }
    for (size_t i = 0; i < depth * 2; ++i) {
        status = json_append_char(writer, ' ', err);
        if (status != CA_OK) {
            return status;
        }
    }
    return CA_OK;
}

static ca_status json_append_escaped(ca_json_writer *writer, const char *data,
                                     size_t length, ca_error *err) {
    ca_status status = json_append_char(writer, '"', err);
    if (status != CA_OK) {
        return status;
    }

    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)data[i];
        switch (c) {
        case '"':
            status = json_append_cstr(writer, "\\\"", err);
            break;
        case '\\':
            status = json_append_cstr(writer, "\\\\", err);
            break;
        case '\b':
            status = json_append_cstr(writer, "\\b", err);
            break;
        case '\f':
            status = json_append_cstr(writer, "\\f", err);
            break;
        case '\n':
            status = json_append_cstr(writer, "\\n", err);
            break;
        case '\r':
            status = json_append_cstr(writer, "\\r", err);
            break;
        case '\t':
            status = json_append_cstr(writer, "\\t", err);
            break;
        default:
            if (c < 0x20) {
                char escaped[7];
                snprintf(escaped, sizeof escaped, "\\u%04x", (unsigned)c);
                status = json_append_cstr(writer, escaped, err);
            } else {
                status = json_append_char(writer, (char)c, err);
            }
            break;
        }
        if (status != CA_OK) {
            return status;
        }
    }

    return json_append_char(writer, '"', err);
}

/**
 * @brief Emit the separator (and key) for the next value.
 *
 * Marks the root as written so a second root value is rejected.
 */
static ca_status json_writer_prefix(ca_json_writer *writer, const char *key,
                                    ca_error *err) {
    ca_json_frame *frame = &writer->frames[writer->depth];

    if (frame->kind == CA_JSON_FRAME_ROOT) {
        if (writer->root_written) {
            if (err) {
                ca_error_set(err, CA_INVALID_ARGUMENT,
                             "JSON writer root value already written");
            }
            return CA_INVALID_ARGUMENT;
        }
        writer->root_written = true;
        return CA_OK;
    }

    if (frame->kind == CA_JSON_FRAME_OBJECT) {
        if (!key) {
            if (err) {
                ca_error_set(err, CA_INVALID_ARGUMENT,
                             "JSON object member requires a key");
            }
            return CA_INVALID_ARGUMENT;
        }
    } else if (key) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT,
                         "JSON array element must not have a key");
        }
        return CA_INVALID_ARGUMENT;
    }

    ca_status status = CA_OK;
    if (frame->count > 0) {
        status = json_append_char(writer, ',', err);
        if (status != CA_OK) {
            return status;
        }
    }

    if (writer->pretty) {
        status = json_append_indent(writer, writer->depth, err);
        if (status != CA_OK) {
            return status;
        }
    }

    if (frame->kind == CA_JSON_FRAME_OBJECT) {
        status = json_append_escaped(writer, key, strlen(key), err);
        if (status != CA_OK) {
            return status;
        }
        status = json_append_char(writer, ':', err);
        if (status != CA_OK) {
            return status;
        }
        if (writer->pretty) {
            status = json_append_char(writer, ' ', err);
            if (status != CA_OK) {
                return status;
            }
        }
    }

    frame->count++;
    return CA_OK;
}

ca_status ca_json_writer_init(ca_json_writer **out, int pretty,
                              const cx_allocator *alloc, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!out) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }
    *out = nullptr;

    const cx_allocator *a = alloc ? alloc : cx_allocator_default();
    ca_json_writer *writer =
        (ca_json_writer *)cx_allocator_alloc(a, sizeof(ca_json_writer),
                                             alignof(ca_json_writer));
    if (!writer) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY,
                         "failed to allocate JSON writer");
        }
        return CA_OUT_OF_MEMORY;
    }

    writer->builder = nullptr;
    writer->alloc = *a;
    writer->depth = 0;
    writer->pretty = pretty != 0;
    writer->root_written = false;
    writer->frames[0].kind = CA_JSON_FRAME_ROOT;
    writer->frames[0].count = 0;

    cx_status status = cx_string_builder_init(&writer->builder, alloc);
    if (status != CX_OK) {
        cx_allocator_free(a, writer, sizeof(ca_json_writer),
                          alignof(ca_json_writer));
        return json_from_cx(status, err);
    }

    *out = writer;
    return CA_OK;
}

void ca_json_writer_destroy(ca_json_writer **out) {
    if (!out || !*out) {
        return;
    }

    ca_json_writer *writer = *out;
    *out = nullptr;

    cx_string_builder_destroy(&writer->builder);
    cx_allocator_free(&writer->alloc, writer, sizeof(ca_json_writer),
                      alignof(ca_json_writer));
}

static ca_status json_begin_container(ca_json_writer *writer, const char *key,
                                      uint8_t kind, char open,
                                      ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!writer) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "writer is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    if (writer->depth >= (size_t)CA_JSON_MAX_DEPTH) {
        if (err) {
            ca_error_set(err, CA_OVERFLOW,
                         "JSON nesting exceeds %d levels", CA_JSON_MAX_DEPTH);
        }
        return CA_OVERFLOW;
    }

    ca_status status = json_writer_prefix(writer, key, err);
    if (status != CA_OK) {
        return status;
    }

    status = json_append_char(writer, open, err);
    if (status != CA_OK) {
        return status;
    }

    writer->depth++;
    writer->frames[writer->depth].kind = kind;
    writer->frames[writer->depth].count = 0;
    return CA_OK;
}

ca_status ca_json_writer_begin_object(ca_json_writer *writer, const char *key,
                                      ca_error *err) {
    return json_begin_container(writer, key, CA_JSON_FRAME_OBJECT, '{', err);
}

ca_status ca_json_writer_begin_array(ca_json_writer *writer, const char *key,
                                     ca_error *err) {
    return json_begin_container(writer, key, CA_JSON_FRAME_ARRAY, '[', err);
}

static ca_status json_end_container(ca_json_writer *writer, uint8_t kind,
                                    char close, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!writer) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "writer is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    ca_json_frame *frame = &writer->frames[writer->depth];
    if (writer->depth == 0 || frame->kind != kind) {
        if (err) {
            ca_error_set(err, CA_JSON_ERROR,
                         "JSON writer is not inside the expected container");
        }
        return CA_JSON_ERROR;
    }

    ca_status status = CA_OK;
    if (writer->pretty && frame->count > 0) {
        status = json_append_indent(writer, writer->depth - 1, err);
        if (status != CA_OK) {
            return status;
        }
    }

    status = json_append_char(writer, close, err);
    if (status != CA_OK) {
        return status;
    }

    writer->depth--;
    return CA_OK;
}

ca_status ca_json_writer_end_object(ca_json_writer *writer, ca_error *err) {
    return json_end_container(writer, CA_JSON_FRAME_OBJECT, '}', err);
}

ca_status ca_json_writer_end_array(ca_json_writer *writer, ca_error *err) {
    return json_end_container(writer, CA_JSON_FRAME_ARRAY, ']', err);
}

ca_status ca_json_writer_add_null(ca_json_writer *writer, const char *key,
                                  ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!writer) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "writer is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }
    ca_status status = json_writer_prefix(writer, key, err);
    if (status != CA_OK) {
        return status;
    }
    return json_append_cstr(writer, "null", err);
}

ca_status ca_json_writer_add_bool(ca_json_writer *writer, const char *key,
                                  bool value, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!writer) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "writer is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }
    ca_status status = json_writer_prefix(writer, key, err);
    if (status != CA_OK) {
        return status;
    }
    return json_append_cstr(writer, value ? "true" : "false", err);
}

ca_status ca_json_writer_add_int(ca_json_writer *writer, const char *key,
                                 int64_t value, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!writer) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "writer is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }
    ca_status status = json_writer_prefix(writer, key, err);
    if (status != CA_OK) {
        return status;
    }
    return json_from_cx(
        cx_string_builder_printf(writer->builder, "%" PRId64, value), err);
}

ca_status ca_json_writer_add_uint(ca_json_writer *writer, const char *key,
                                  uint64_t value, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!writer) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "writer is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }
    ca_status status = json_writer_prefix(writer, key, err);
    if (status != CA_OK) {
        return status;
    }
    return json_from_cx(
        cx_string_builder_printf(writer->builder, "%" PRIu64, value), err);
}

/**
 * @brief Format a double with the shortest representation that round-trips.
 *
 * @param buffer The buffer to write the formatted string to.
 * @param size The size of the buffer.
 * @param value The double value to format.
 * @return false for non-finite values, which the caller writes as null.
 */
static bool json_format_double(char *buffer, size_t size, double value) {
    if (!isfinite(value)) {
        return false;
    }

    for (int precision = 1; precision <= 17; ++precision) {
        int written = snprintf(buffer, size, "%.*g", precision, value);
        if (written < 0 || (size_t)written >= size) {
            return false;
        }
        char *end = nullptr;
        double parsed = strtod(buffer, &end);
        if (end && *end == '\0' && parsed == value) {
            break;
        }
    }
    return true;
}

ca_status ca_json_writer_add_double(ca_json_writer *writer, const char *key,
                                    double value, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!writer) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "writer is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }
    ca_status status = json_writer_prefix(writer, key, err);
    if (status != CA_OK) {
        return status;
    }

    char buffer[40];
    if (!json_format_double(buffer, sizeof buffer, value)) {
        return json_append_cstr(writer, "null", err);
    }

    status = json_append_cstr(writer, buffer, err);
    if (status != CA_OK) {
        return status;
    }

    /* Keep the floating-point nature of integral values ("2" -> "2.0"), as
     * the C++ wire format does. */
    if (!strpbrk(buffer, ".eE")) {
        status = json_append_cstr(writer, ".0", err);
    }
    return status;
}

ca_status ca_json_writer_add_string(ca_json_writer *writer, const char *key,
                                    const char *value, size_t length,
                                    ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!writer) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "writer is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }
    if (!value && length != 0) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT,
                         "string value is nullptr but length is nonzero");
        }
        return CA_INVALID_ARGUMENT;
    }

    ca_status status = json_writer_prefix(writer, key, err);
    if (status != CA_OK) {
        return status;
    }
    return json_append_escaped(writer, value ? value : "", length, err);
}

ca_status ca_json_writer_add_cstr(ca_json_writer *writer, const char *key,
                                  const char *value, ca_error *err) {
    return ca_json_writer_add_string(writer, key, value,
                                     value ? strlen(value) : 0, err);
}

static ca_status json_write_value(ca_json_writer *writer, const char *key,
                                  const ca_json_val *val, ca_error *err) {
    switch (ca_json_type_of(val)) {
    case CA_JSON_NULL:
        return ca_json_writer_add_null(writer, key, err);
    case CA_JSON_TRUE:
        return ca_json_writer_add_bool(writer, key, true, err);
    case CA_JSON_FALSE:
        return ca_json_writer_add_bool(writer, key, false, err);
    case CA_JSON_INT:
        return ca_json_writer_add_int(writer, key, ca_json_get_int(val), err);
    case CA_JSON_UINT:
        return ca_json_writer_add_uint(writer, key, ca_json_get_uint(val), err);
    case CA_JSON_DOUBLE:
        return ca_json_writer_add_double(writer, key, ca_json_get_double(val),
                                         err);
    case CA_JSON_STRING: {
        size_t length = 0;
        const char *text = ca_json_get_string(val, &length);
        return ca_json_writer_add_string(writer, key, text, length, err);
    }
    case CA_JSON_OBJECT: {
        ca_status status = ca_json_writer_begin_object(writer, key, err);
        if (status != CA_OK) {
            return status;
        }
        size_t count = ca_json_object_size(val);
        for (size_t i = 0; i < count; ++i) {
            const char *member = ca_json_object_key(val, i, nullptr);
            if (member == nullptr) {
                continue;
            }
            status = json_write_value(writer, member,
                                      ca_json_object_get(val, member), err);
            if (status != CA_OK) {
                return status;
            }
        }
        return ca_json_writer_end_object(writer, err);
    }
    case CA_JSON_ARRAY: {
        ca_status status = ca_json_writer_begin_array(writer, key, err);
        if (status != CA_OK) {
            return status;
        }
        size_t count = ca_json_array_size(val);
        for (size_t i = 0; i < count; ++i) {
            status = json_write_value(writer, nullptr, ca_json_array_get(val, i),
                                      err);
            if (status != CA_OK) {
                return status;
            }
        }
        return ca_json_writer_end_array(writer, err);
    }
    }

    /* Unreachable for parsed values; emit null for forward compatibility. */
    return ca_json_writer_add_null(writer, key, err);
}

ca_status ca_json_writer_add_value(ca_json_writer *writer, const char *key,
                                   const ca_json_val *val, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (!writer) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "writer is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }
    return json_write_value(writer, key, val, err);
}

ca_status ca_json_writer_finish(ca_json_writer *writer, cx_string **out,
                                ca_error *err) {
    if (err) {
        ca_error_init(err);
    }
    if (out) {
        *out = nullptr;
    }
    if (!writer || !out) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "writer or out is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }
    if (writer->depth != 0 || !writer->root_written) {
        if (err) {
            ca_error_set(err, CA_JSON_ERROR,
                         "JSON document is incomplete");
        }
        return CA_JSON_ERROR;
    }

    return json_from_cx(cx_string_builder_finish(writer->builder, out), err);
}
