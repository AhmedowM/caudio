#include <caudio/runtime/json.h>

#include <cstdx/allocator.h>
#include <stddef.h>
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

struct ca_json {
    yyjson_doc *doc;
    cx_allocator alloc; /* copy of the allocator vtable */
};

/**
 * @brief cx_allocator adapter for yyjson's yyjson_alc interface.
 *
 * cx_allocator_free requires the size and alignment used at allocation time.
 * yyjson's free callback only receives the pointer, so we track the size in a
 * side table keyed by the returned pointer. This keeps the contract honest
 * without adding per-allocation overhead in the common case.
 */
typedef struct ca_json_alloc_ctx {
    const cx_allocator *alloc;
} ca_json_alloc_ctx;

static void *ca_json_malloc(void *ctx, size_t size) {
    ca_json_alloc_ctx *c = (ca_json_alloc_ctx *)ctx;
    return cx_allocator_alloc(c->alloc, size, 0);
}

static void *ca_json_realloc(void *ctx, void *ptr, size_t old_size,
                             size_t new_size) {
    ca_json_alloc_ctx *c = (ca_json_alloc_ctx *)ctx;
    return cx_allocator_realloc(c->alloc, ptr, old_size, new_size, 0);
}

static void ca_json_free(void *ctx, void *ptr) {
    ca_json_alloc_ctx *c = (ca_json_alloc_ctx *)ctx;
    cx_allocator_free(c->alloc, ptr, 0, 0);
}

static yyjson_alc ca_json_make_alc(const cx_allocator *alloc) {
    ca_json_alloc_ctx *ctx = cx_allocator_alloc(alloc, sizeof(ca_json_alloc_ctx),
                                                alignof(ca_json_alloc_ctx));
    if (!ctx) {
        return (yyjson_alc){NULL, NULL, NULL, NULL};
    }
    ctx->alloc = alloc;
    return (yyjson_alc){
        .malloc = ca_json_malloc,
        .realloc = ca_json_realloc,
        .free = ca_json_free,
        .ctx = ctx,
    };
}

static void ca_json_free_alc(yyjson_alc alc) {
    if (alc.ctx) {
        cx_allocator_free(alc.ctx, alc.ctx, sizeof(ca_json_alloc_ctx), 0);
    }
}

ca_status ca_json_parse(ca_json **out, const char *text, size_t len,
                        const cx_allocator *alloc, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (!out || !text) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out or text is NULL");
        }
        return CA_INVALID_ARGUMENT;
    }

    if (len == 0) {
        len = strlen(text);
    }

    yyjson_alc alc = ca_json_make_alc(alloc ? alloc : cx_allocator_default());
    if (!alc.ctx) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate allocator");
        }
        return CA_OUT_OF_MEMORY;
    }

    yyjson_read_err rerr = {0};
    yyjson_doc *doc = yyjson_read_opts((char *)text, len, 0, &alc, &rerr);
    ca_json_free_alc(alc);

    if (!doc) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "JSON parse error: %s at "
                             "byte %lu", rerr.msg, (unsigned long)rerr.pos);
        }
        return CA_INVALID_ARGUMENT;
    }

    ca_json *json = cx_allocator_alloc(alloc ? alloc : cx_allocator_default(),
                                       sizeof(ca_json), 0);
    if (!json) {
        yyjson_doc_free(doc);
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate handle");
        }
        return CA_OUT_OF_MEMORY;
    }

    json->doc = doc;
    json->alloc = alloc ? *alloc : *cx_allocator_default();
    *out = json;
    return CA_OK;
}

ca_status ca_json_parse_file(ca_json **out, const char *path,
                             const cx_allocator *alloc, ca_error *err) {
    if (err) {
        ca_error_init(err);
    }

    if (!out || !path) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "out or path is NULL");
        }
        return CA_INVALID_ARGUMENT;
    }

    yyjson_alc alc = ca_json_make_alc(alloc ? alloc : cx_allocator_default());
    if (!alc.ctx) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate allocator");
        }
        return CA_OUT_OF_MEMORY;
    }

    yyjson_read_err rerr = {0};
    yyjson_doc *doc = yyjson_read_file(path, 0, &alc, &rerr);
    ca_json_free_alc(alc);

    if (!doc) {
        if (err) {
            ca_error_set(err, CA_IO_ERROR, "failed to read JSON file: %s",
                         rerr.msg);
        }
        return CA_IO_ERROR;
    }

    ca_json *json = cx_allocator_alloc(alloc ? alloc : cx_allocator_default(),
                                       sizeof(ca_json), 0);
    if (!json) {
        yyjson_doc_free(doc);
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate handle");
        }
        return CA_OUT_OF_MEMORY;
    }

    json->doc = doc;
    json->alloc = alloc ? *alloc : *cx_allocator_default();
    *out = json;
    return CA_OK;
}

void ca_json_destroy(ca_json **out) {
    if (!out || !*out) {
        return;
    }

    ca_json *json = *out;
    const cx_allocator *alloc = &json->alloc;

    yyjson_doc_free(json->doc);
    cx_allocator_free(alloc, json, sizeof(ca_json), 0);
    *out = NULL;
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

    if (!json) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "json is NULL");
        }
        if (len) {
            *len = 0;
        }
        return NULL;
    }

    const cx_allocator *a = alloc ? alloc : cx_allocator_default();
    yyjson_alc yalc = ca_json_make_alc(a);
    if (!yalc.ctx) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to allocate allocator");
        }
        if (len) {
            *len = 0;
        }
        return NULL;
    }

    yyjson_write_flag flg = pretty ? YYJSON_WRITE_PRETTY : YYJSON_WRITE_NOFLAG;
    yyjson_write_err werr = {0};
    char *out = yyjson_mut_write_opts((const yyjson_mut_doc *)json->doc, flg,
                                      &yalc, len, &werr);
    ca_json_free_alc(yalc);

    if (!out) {
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "failed to serialize JSON");
        }
        if (len) {
            *len = 0;
        }
        return NULL;
    }

    return out;
}
