#include <caudio/runtime/fs.h>
#include <caudio/runtime/json.h>

#include <cstdx/allocator.h>
#include <cstdx/allocator_counted.h>
#include <cstdx/string.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ca_test.h"

static const char *JSON_FILE = "caudio_test_json_file.json";

/* Build "[0,0,...,0]" with the requested element count. */
static char *make_array_text(size_t count, size_t *out_len) {
    size_t len = count == 0 ? 2 : 1 + count + (count - 1) + 1;
    char *text = (char *)malloc(len + 1);
    if (!text) {
        return NULL;
    }

    size_t pos = 0;
    text[pos++] = '[';
    for (size_t i = 0; i < count; ++i) {
        text[pos++] = '0';
        if (i + 1 < count) {
            text[pos++] = ',';
        }
    }
    text[pos++] = ']';
    text[pos] = '\0';

    if (out_len) {
        *out_len = pos;
    }
    return text;
}

/* Build "[[...[0]...]]" with the requested container depth. */
static char *make_nested_text(size_t depth, size_t *out_len) {
    char *text = (char *)malloc(2 * depth + 2);
    if (!text) {
        return NULL;
    }

    size_t pos = 0;
    for (size_t i = 0; i < depth; ++i) {
        text[pos++] = '[';
    }
    text[pos++] = '0';
    for (size_t i = 0; i < depth; ++i) {
        text[pos++] = ']';
    }
    text[pos] = '\0';

    if (out_len) {
        *out_len = pos;
    }
    return text;
}

static ca_json *must_parse(const char *text, size_t len,
                           const cx_allocator *alloc) {
    ca_error err;
    ca_error_init(&err);

    ca_json *json = NULL;
    ca_status status = ca_json_parse(&json, text, len, alloc, &err);
    if (status != CA_OK) {
        fprintf(stderr, "parse failed: %s\n", err.message);
        ca_test_failures++;
        return NULL;
    }

    return json;
}

static int test_scalars(void) {
    const char *text = "{\"null\":null,\"t\":true,\"f\":false,\"i\":-42,"
                       "\"u\":42,\"d\":1.5,\"s\":\"hi\"}";
    ca_json *json = must_parse(text, 0, NULL);
    const ca_json_val *root = ca_json_root(json);

    CA_CHECK(root != NULL);
    CA_CHECK_EQ(CA_JSON_OBJECT, ca_json_type_of(root));
    CA_CHECK_EQ(7, ca_json_object_size(root));

    const ca_json_val *val = ca_json_object_get(root, "null");
    CA_CHECK_EQ(CA_JSON_NULL, ca_json_type_of(val));
    val = ca_json_object_get(root, "t");
    CA_CHECK_EQ(CA_JSON_TRUE, ca_json_type_of(val));
    val = ca_json_object_get(root, "f");
    CA_CHECK_EQ(CA_JSON_FALSE, ca_json_type_of(val));

    const ca_json_val *i = ca_json_object_get(root, "i");
    CA_CHECK_EQ(CA_JSON_INT, ca_json_type_of(i));
    CA_CHECK_EQ(-42, ca_json_get_int(i));
    CA_CHECK_EQ(0, ca_json_get_uint(i));

    const ca_json_val *u = ca_json_object_get(root, "u");
    CA_CHECK_EQ(CA_JSON_UINT, ca_json_type_of(u));
    CA_CHECK_EQ(42, ca_json_get_uint(u));
    CA_CHECK_EQ(0, ca_json_get_int(u));

    const ca_json_val *d = ca_json_object_get(root, "d");
    CA_CHECK_EQ(CA_JSON_DOUBLE, ca_json_type_of(d));
    CA_CHECK(ca_json_get_double(d) == 1.5);

    const ca_json_val *s = ca_json_object_get(root, "s");
    CA_CHECK_EQ(CA_JSON_STRING, ca_json_type_of(s));
    size_t len = 99;
    const char *str = ca_json_get_string(s, &len);
    CA_CHECK(str != NULL);
    CA_CHECK_EQ(2, len);
    CA_CHECK_EQ(0, strcmp(str, "hi"));

    /* Wrong-type accessors return their neutral value. */
    CA_CHECK(ca_json_get_string(i, &len) == NULL);
    CA_CHECK_EQ(0, len);
    CA_CHECK_EQ(0, ca_json_get_int(s));
    CA_CHECK(ca_json_get_double(u) == 0.0);

    ca_json_destroy(&json);
    CA_CHECK(json == NULL);
    return ca_test_finish("json.scalars");
}

static int test_arrays(void) {
    ca_json *json = must_parse("[1,2,3]", 0, NULL);
    const ca_json_val *root = ca_json_root(json);

    CA_CHECK(root != NULL);
    CA_CHECK_EQ(CA_JSON_ARRAY, ca_json_type_of(root));
    CA_CHECK_EQ(3, ca_json_array_size(root));
    CA_CHECK_EQ(1, ca_json_get_uint(ca_json_array_get(root, 0)));
    CA_CHECK_EQ(2, ca_json_get_uint(ca_json_array_get(root, 1)));
    CA_CHECK_EQ(3, ca_json_get_uint(ca_json_array_get(root, 2)));
    CA_CHECK(ca_json_array_get(root, 3) == NULL);
    CA_CHECK_EQ(0, ca_json_object_size(root));
    CA_CHECK(ca_json_object_get(root, "x") == NULL);

    ca_json_destroy(&json);

    json = must_parse("{\"a\":1}", 0, NULL);
    root = ca_json_root(json);
    CA_CHECK_EQ(0, ca_json_array_size(root));
    CA_CHECK(ca_json_array_get(root, 0) == NULL);

    ca_json_destroy(&json);
    return ca_test_finish("json.arrays");
}

static int test_objects(void) {
    const char *text = "{\"a\":1,\"b\":{\"c\":2},\"arr\":[true]}";
    ca_json *json = must_parse(text, 0, NULL);
    const ca_json_val *root = ca_json_root(json);

    CA_CHECK_EQ(3, ca_json_object_size(root));
    CA_CHECK_EQ(1, ca_json_get_uint(ca_json_object_get(root, "a")));

    const ca_json_val *b = ca_json_object_get(root, "b");
    CA_CHECK_EQ(CA_JSON_OBJECT, ca_json_type_of(b));
    CA_CHECK_EQ(1, ca_json_object_size(b));
    CA_CHECK_EQ(2, ca_json_get_uint(ca_json_object_get(b, "c")));

    CA_CHECK(ca_json_object_get(root, "missing") == NULL);
    CA_CHECK(ca_json_object_get(root, NULL) == NULL);
    CA_CHECK(ca_json_object_get(NULL, "a") == NULL);

    ca_json_destroy(&json);
    return ca_test_finish("json.objects");
}

static int test_strings(void) {
    ca_json *json = must_parse("\"h\\u00e9llo\"", 0, NULL);
    size_t len = 0;
    const char *str = ca_json_get_string(ca_json_root(json), &len);

    CA_CHECK(str != NULL);
    CA_CHECK_EQ(6, len);
    CA_CHECK_EQ(0, memcmp(str, "h\xc3\xa9llo", 6));
    ca_json_destroy(&json);

    /* Embedded NUL: allowed in the value, length is authoritative. */
    json = must_parse("\"a\\u0000b\"", 0, NULL);
    str = ca_json_get_string(ca_json_root(json), &len);
    CA_CHECK(str != NULL);
    CA_CHECK_EQ(3, len);
    CA_CHECK(str[0] == 'a' && str[1] == '\0' && str[2] == 'b');
    ca_json_destroy(&json);

    /* Escaped quote and backslash. */
    json = must_parse("\"a\\\"b\\\\c\"", 0, NULL);
    str = ca_json_get_string(ca_json_root(json), &len);
    CA_CHECK(str != NULL);
    CA_CHECK_EQ(5, len);
    CA_CHECK_EQ(0, strcmp(str, "a\"b\\c"));
    ca_json_destroy(&json);

    return ca_test_finish("json.strings");
}

static int test_numbers(void) {
    const char *text = "[9223372036854775807,18446744073709551615,"
                       "-9223372036854775808,1.0e3,0.0]";
    ca_json *json = must_parse(text, 0, NULL);
    const ca_json_val *root = ca_json_root(json);

    CA_CHECK_EQ(5, ca_json_array_size(root));

    const ca_json_val *v = ca_json_array_get(root, 0);
    CA_CHECK_EQ(CA_JSON_UINT, ca_json_type_of(v));
    CA_CHECK(ca_json_get_uint(v) == UINT64_C(9223372036854775807));

    v = ca_json_array_get(root, 1);
    CA_CHECK_EQ(CA_JSON_UINT, ca_json_type_of(v));
    CA_CHECK(ca_json_get_uint(v) == UINT64_MAX);

    v = ca_json_array_get(root, 2);
    CA_CHECK_EQ(CA_JSON_INT, ca_json_type_of(v));
    CA_CHECK(ca_json_get_int(v) == INT64_MIN);

    v = ca_json_array_get(root, 3);
    CA_CHECK_EQ(CA_JSON_DOUBLE, ca_json_type_of(v));
    CA_CHECK(ca_json_get_double(v) == 1000.0);

    v = ca_json_array_get(root, 4);
    CA_CHECK_EQ(CA_JSON_DOUBLE, ca_json_type_of(v));
    CA_CHECK(ca_json_get_double(v) == 0.0);

    ca_json_destroy(&json);
    return ca_test_finish("json.numbers");
}

static int test_explicit_length(void) {
    const char *text = "{}garbage";
    ca_error err;
    ca_error_init(&err);

    ca_json *json = NULL;
    CA_CHECK_EQ(CA_OK, ca_json_parse(&json, text, 2, NULL, &err));
    CA_CHECK(json != NULL);
    ca_json_destroy(&json);

    json = NULL;
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_json_parse(&json, text, 0, NULL, &err));
    CA_CHECK(json == NULL);

    return ca_test_finish("json.explicit_length");
}

static int test_invalid(void) {
    static const char *const invalid[] = {
        "", "{", "[1,]", "[1] junk", "nul", "\"unterminated", "{,}",
        "{} {}", "01",
    };

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        ca_error err;
        ca_error_init(&err);

        ca_json *json = (ca_json *)(uintptr_t)1;
        ca_status status = ca_json_parse(&json, invalid[i], 0, NULL, &err);

        CA_CHECK_EQ(CA_INVALID_ARGUMENT, status);
        CA_CHECK(json == NULL);
        CA_CHECK_EQ(CA_INVALID_ARGUMENT, err.code);
        CA_CHECK(err.message[0] != '\0');
    }

    ca_error err;
    ca_error_init(&err);
    ca_json *json = NULL;

    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_json_parse(NULL, "{}", 0, NULL, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_json_parse(&json, NULL, 0, NULL, &err));
    CA_CHECK(json == NULL);

    return ca_test_finish("json.invalid");
}

static int test_null_handles(void) {
    CA_CHECK(ca_json_root(NULL) == NULL);
    CA_CHECK_EQ(CA_JSON_NULL, ca_json_type_of(NULL));
    CA_CHECK(ca_json_get_string(NULL, NULL) == NULL);
    CA_CHECK_EQ(0, ca_json_get_int(NULL));
    CA_CHECK_EQ(0, ca_json_get_uint(NULL));
    CA_CHECK(ca_json_get_double(NULL) == 0.0);
    CA_CHECK_EQ(0, ca_json_array_size(NULL));
    CA_CHECK(ca_json_array_get(NULL, 0) == NULL);
    CA_CHECK_EQ(0, ca_json_object_size(NULL));
    CA_CHECK(ca_json_object_get(NULL, "a") == NULL);

    ca_json_destroy(NULL);
    ca_json *json = NULL;
    ca_json_destroy(&json);
    CA_CHECK(json == NULL);

    return ca_test_finish("json.null_handles");
}

static int test_serialize(void) {
    const char *text = "{\"a\":1,\"b\":[true,null,\"x\"],\"d\":1.5}";
    ca_json *json = must_parse(text, 0, NULL);
    ca_error err;
    ca_error_init(&err);

    size_t len = 0;
    char *compact = ca_json_serialize(json, 0, &len, NULL, &err);
    CA_CHECK(compact != NULL);
    CA_CHECK_EQ(CA_OK, err.code);
    CA_CHECK_EQ(len, strlen(compact));
    CA_CHECK(strstr(compact, "\"a\":1") != NULL);

    ca_json *round = must_parse(compact, len, NULL);
    CA_CHECK(round != NULL);
    if (round) {
        const ca_json_val *root = ca_json_root(round);
        const ca_json_val *a = ca_json_object_get(root, "a");
        CA_CHECK_EQ(CA_JSON_UINT, ca_json_type_of(a));
        CA_CHECK_EQ(1, ca_json_get_uint(a));
        CA_CHECK_EQ(3, ca_json_array_size(ca_json_object_get(root, "b")));
        CA_CHECK(ca_json_get_double(ca_json_object_get(root, "d")) == 1.5);
        ca_json_destroy(&round);
    }

    cx_allocator_free(cx_allocator_default(), compact, len + 1, 0);

    size_t pretty_len = 0;
    char *pretty = ca_json_serialize(json, 1, &pretty_len, NULL, &err);
    CA_CHECK(pretty != NULL);
    CA_CHECK_EQ(pretty_len, strlen(pretty));
    CA_CHECK(strchr(pretty, '\n') != NULL);
    cx_allocator_free(cx_allocator_default(), pretty, pretty_len + 1, 0);

    len = 42;
    CA_CHECK(ca_json_serialize(NULL, 0, &len, NULL, &err) == NULL);
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, err.code);
    CA_CHECK_EQ(0, len);

    ca_json_destroy(&json);
    return ca_test_finish("json.serialize");
}

static int test_allocator(void) {
    cx_allocator_counted counted;
    CA_CHECK_EQ(CX_OK, cx_allocator_counted_init(&counted, NULL));
    cx_allocator alloc = cx_allocator_counted_as_allocator(&counted);

    ca_json *json = must_parse("{\"a\":[1,2,3],\"s\":\"text\"}", 0, &alloc);
    CA_CHECK(json != NULL);
    ca_json_destroy(&json);
    CA_CHECK_EQ(0, counted.live_blocks);
    CA_CHECK(!cx_allocator_counted_has_leaks(&counted));

    ca_error err;
    ca_error_init(&err);
    json = NULL;
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_json_parse(&json, "{", 0, &alloc, &err));
    CA_CHECK_EQ(0, counted.live_blocks);

    json = must_parse("{\"a\":1}", 0, &alloc);
    size_t len = 0;
    char *serialized = ca_json_serialize(json, 0, &len, &alloc, &err);
    CA_CHECK(serialized != NULL);
    CA_CHECK_EQ(CA_OK, err.code);
    cx_allocator_free(&alloc, serialized, len + 1, 0);
    ca_json_destroy(&json);
    CA_CHECK_EQ(0, counted.live_blocks);
    CA_CHECK(!cx_allocator_counted_has_leaks(&counted));

    cx_allocator_counted_destroy(&counted);
    return ca_test_finish("json.allocator");
}

static int test_parse_file(void) {
    ca_error err;
    ca_error_init(&err);

    FILE *file = fopen(JSON_FILE, "wb");
    CA_CHECK(file != NULL);
    if (file) {
        const char *text = "{\"file\":true}";
        CA_CHECK_EQ(strlen(text), fwrite(text, 1, strlen(text), file));
        CA_CHECK_EQ(0, fclose(file));
    }

    ca_json *json = NULL;
    CA_CHECK_EQ(CA_OK, ca_json_parse_file(&json, JSON_FILE, NULL, &err));
    CA_CHECK(json != NULL);
    CA_CHECK_EQ(CA_JSON_TRUE,
                ca_json_type_of(
                    ca_json_object_get(ca_json_root(json), "file")));
    ca_json_destroy(&json);

    /* The transient file-read buffer must also come from, and return to, the
     * supplied allocator. */
    cx_allocator_counted counted;
    CA_CHECK_EQ(CX_OK, cx_allocator_counted_init(&counted, NULL));
    cx_allocator alloc = cx_allocator_counted_as_allocator(&counted);
    CA_CHECK_EQ(CA_OK, ca_json_parse_file(&json, JSON_FILE, &alloc, &err));
    CA_CHECK(json != NULL);
    ca_json_destroy(&json);
    CA_CHECK_EQ(0, counted.live_blocks);
    CA_CHECK(!cx_allocator_counted_has_leaks(&counted));
    cx_allocator_counted_destroy(&counted);

    json = (ca_json *)(uintptr_t)1;
    CA_CHECK_EQ(CA_IO_ERROR,
                ca_json_parse_file(&json, "caudio_test_json_missing.json", NULL,
                                   &err));
    CA_CHECK(json == NULL);

    json = (ca_json *)(uintptr_t)1;
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_json_parse_file(&json, NULL, NULL, &err));
    CA_CHECK(json == NULL);

    json = (ca_json *)(uintptr_t)1;
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_json_parse_file(&json, "", NULL, &err));
    CA_CHECK(json == NULL);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_json_parse_file(NULL, JSON_FILE, NULL, &err));

    file = fopen(JSON_FILE, "wb");
    CA_CHECK(file != NULL);
    if (file) {
        const char *text = "{not json";
        CA_CHECK_EQ(strlen(text), fwrite(text, 1, strlen(text), file));
        CA_CHECK_EQ(0, fclose(file));
    }

    json = NULL;
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_json_parse_file(&json, JSON_FILE, NULL, &err));
    CA_CHECK(json == NULL);

    CA_CHECK_EQ(0, remove(JSON_FILE));
    return ca_test_finish("json.parse_file");
}

static int test_input_limit(void) {
    ca_error err;
    ca_error_init(&err);

    /* The cap is checked before the input is inspected, so it is safe to pair
     * a tiny string with an oversized length. */
    ca_json *json = (ca_json *)(uintptr_t)1;
    CA_CHECK_EQ(CA_OVERFLOW,
                ca_json_parse(&json, "{}", CA_JSON_MAX_BYTES + 1, NULL, &err));
    CA_CHECK(json == NULL);
    CA_CHECK_EQ(CA_OVERFLOW, err.code);
    CA_CHECK(err.message[0] != '\0');

    return ca_test_finish("json.input_limit");
}

static int test_depth_limit(void) {
    ca_error err;
    ca_error_init(&err);

    /* Exactly at the cap is accepted. */
    size_t len = 0;
    char *text = make_nested_text(CA_JSON_MAX_DEPTH, &len);
    CA_CHECK(text != NULL);
    ca_json *json = NULL;
    CA_CHECK_EQ(CA_OK, ca_json_parse(&json, text, len, NULL, &err));
    CA_CHECK(json != NULL);
    ca_json_destroy(&json);
    free(text);

    /* One level deeper is rejected. */
    ca_error_init(&err);
    text = make_nested_text(CA_JSON_MAX_DEPTH + 1, &len);
    CA_CHECK(text != NULL);
    json = (ca_json *)(uintptr_t)1;
    CA_CHECK_EQ(CA_OVERFLOW, ca_json_parse(&json, text, len, NULL, &err));
    CA_CHECK(json == NULL);
    CA_CHECK_EQ(CA_OVERFLOW, err.code);
    free(text);

    return ca_test_finish("json.depth_limit");
}

static int test_container_limit(void) {
    ca_error err;
    ca_error_init(&err);

    /* Exactly at the cap is accepted. */
    size_t len = 0;
    char *text = make_array_text(CA_JSON_MAX_CONTAINER, &len);
    CA_CHECK(text != NULL);
    ca_json *json = NULL;
    CA_CHECK_EQ(CA_OK, ca_json_parse(&json, text, len, NULL, &err));
    CA_CHECK(json != NULL);
    if (json) {
        CA_CHECK_EQ(CA_JSON_MAX_CONTAINER,
                    ca_json_array_size(ca_json_root(json)));
    }
    ca_json_destroy(&json);
    free(text);

    /* One entry over the cap is rejected after parsing. */
    ca_error_init(&err);
    text = make_array_text((size_t)CA_JSON_MAX_CONTAINER + 1, &len);
    CA_CHECK(text != NULL);
    json = (ca_json *)(uintptr_t)1;
    CA_CHECK_EQ(CA_OVERFLOW, ca_json_parse(&json, text, len, NULL, &err));
    CA_CHECK(json == NULL);
    CA_CHECK_EQ(CA_OVERFLOW, err.code);
    CA_CHECK(err.message[0] != '\0');
    free(text);

    return ca_test_finish("json.container_limit");
}

static int test_parse_file_unicode(void) {
    /* "caudio_test_json_café.json" in UTF-8. */
    const char *path = "caudio_test_json_caf\xc3\xa9.json";

    ca_error err;
    ca_error_init(&err);
    (void)ca_fs_remove(path, NULL);

    ca_file_t file =
        ca_fs_open(path, CA_FS_WRITE | CA_FS_CREATE | CA_FS_TRUNCATE, &err);
    CA_CHECK(file != CA_FS_INVALID);
    if (file != CA_FS_INVALID) {
        const char *text = "{\"unicode\":true}";
        CA_CHECK_EQ((long long)strlen(text),
                    (long long)ca_fs_write(file, text, strlen(text), &err));
        ca_fs_close(file);
    }

    ca_json *json = NULL;
    CA_CHECK_EQ(CA_OK, ca_json_parse_file(&json, path, NULL, &err));
    CA_CHECK(json != NULL);
    if (json) {
        const ca_json_val *root = ca_json_root(json);
        CA_CHECK_EQ(CA_JSON_TRUE,
                    ca_json_type_of(ca_json_object_get(root, "unicode")));
        ca_json_destroy(&json);
    }

    CA_CHECK_EQ(CA_OK, ca_fs_remove(path, &err));
    return ca_test_finish("json.parse_file_unicode");
}

static int test_object_keys(void) {
    ca_json *json = must_parse("{\"a\":1,\"b\":2,\"c\":3}", 0, NULL);
    const ca_json_val *root = ca_json_root(json);
    size_t len = 99;

    const char *key = ca_json_object_key(root, 0, &len);
    CA_CHECK(key != NULL && strcmp(key, "a") == 0);
    CA_CHECK_EQ(1, len);

    key = ca_json_object_key(root, 1, NULL);
    CA_CHECK(key != NULL && strcmp(key, "b") == 0);

    key = ca_json_object_key(root, 2, &len);
    CA_CHECK(key != NULL && strcmp(key, "c") == 0);

    /* Out of range and NULL reset the reported length. */
    CA_CHECK(ca_json_object_key(root, 3, &len) == NULL);
    CA_CHECK_EQ(0, len);
    CA_CHECK(ca_json_object_key(NULL, 0, &len) == NULL);
    CA_CHECK_EQ(0, len);

    ca_json_destroy(&json);

    /* Arrays have no keys. */
    json = must_parse("[1]", 0, NULL);
    CA_CHECK(ca_json_object_key(ca_json_root(json), 0, &len) == NULL);
    CA_CHECK_EQ(0, len);
    ca_json_destroy(&json);
    return ca_test_finish("json.object_keys");
}

static int test_writer(void) {
    ca_error err;
    ca_error_init(&err);

    ca_json_writer *writer = NULL;
    cx_string *text = NULL;

    /* Pretty output, including nested containers and number formatting. */
    CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 1, NULL, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_begin_object(writer, NULL, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_add_cstr(writer, "name", "Alice", &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_add_int(writer, "count", -2, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_add_uint(
                           writer, "big", UINT64_C(18446744073709551615),
                           &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_add_double(writer, "ratio", 0.5, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_add_double(writer, "whole", 2.0, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_add_double(writer, "nan", NAN, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_add_bool(writer, "ok", true, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_add_null(writer, "none", &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_begin_array(writer, "list", &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_add_int(writer, NULL, 1, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_begin_object(writer, NULL, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_end_object(writer, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_end_array(writer, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_end_object(writer, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_finish(writer, &text, &err));
    CA_CHECK(text != NULL);
    if (text) {
        const char *expected =
            "{\n"
            "  \"name\": \"Alice\",\n"
            "  \"count\": -2,\n"
            "  \"big\": 18446744073709551615,\n"
            "  \"ratio\": 0.5,\n"
            "  \"whole\": 2.0,\n"
            "  \"nan\": null,\n"
            "  \"ok\": true,\n"
            "  \"none\": null,\n"
            "  \"list\": [\n"
            "    1,\n"
            "    {}\n"
            "  ]\n"
            "}";
        CA_CHECK_EQ(0, strcmp(cx_string_cstr(text), expected));
    }
    cx_string_destroy(&text);
    ca_json_writer_destroy(&writer);
    CA_CHECK(writer == NULL);

    /* Compact output and a second root value are rejected. */
    CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, NULL, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_add_cstr(writer, NULL, "", &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_json_writer_add_int(writer, NULL, 1, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_finish(writer, &text, &err));
    CA_CHECK(text != NULL && strcmp(cx_string_cstr(text), "\"\"") == 0);
    cx_string_destroy(&text);
    ca_json_writer_destroy(&writer);

    /* Object members need a key. */
    CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, NULL, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_begin_object(writer, NULL, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_json_writer_add_int(writer, NULL, 1, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_end_object(writer, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_finish(writer, &text, &err));
    CA_CHECK(text != NULL && strcmp(cx_string_cstr(text), "{}") == 0);
    cx_string_destroy(&text);
    ca_json_writer_destroy(&writer);

    /* Array elements must not have one. */
    CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, NULL, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_begin_array(writer, NULL, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_json_writer_add_int(writer, "key", 1, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_end_array(writer, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_finish(writer, &text, &err));
    CA_CHECK(text != NULL && strcmp(cx_string_cstr(text), "[]") == 0);
    cx_string_destroy(&text);
    ca_json_writer_destroy(&writer);

    /* Closing the wrong container and an incomplete document are errors. */
    CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, NULL, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_begin_object(writer, NULL, &err));
    CA_CHECK_EQ(CA_JSON_ERROR, ca_json_writer_end_array(writer, &err));
    CA_CHECK_EQ(CA_JSON_ERROR, ca_json_writer_finish(writer, &text, &err));
    CA_CHECK(text == NULL);
    ca_json_writer_destroy(&writer);

    CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, NULL, &err));
    CA_CHECK_EQ(CA_JSON_ERROR, ca_json_writer_finish(writer, &text, &err));
    ca_json_writer_destroy(&writer);

    /* The writer enforces the parser's depth cap. */
    CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, NULL, &err));
    for (int i = 0; i < CA_JSON_MAX_DEPTH; ++i) {
        CA_CHECK_EQ(CA_OK, ca_json_writer_begin_array(writer, NULL, &err));
    }
    CA_CHECK_EQ(CA_OVERFLOW, ca_json_writer_begin_array(writer, NULL, &err));
    for (int i = 0; i < CA_JSON_MAX_DEPTH; ++i) {
        CA_CHECK_EQ(CA_OK, ca_json_writer_end_array(writer, &err));
    }
    CA_CHECK_EQ(CA_OK, ca_json_writer_finish(writer, &text, &err));
    cx_string_destroy(&text);
    ca_json_writer_destroy(&writer);

    /* NULL handles and malformed strings are rejected. */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_json_writer_init(NULL, 0, NULL, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_json_writer_add_null(NULL, NULL, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, NULL, &err));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_json_writer_add_string(writer, NULL, NULL, 1, &err));
    ca_json_writer_destroy(&writer);
    ca_json_writer_destroy(NULL);

    return ca_test_finish("json.writer");
}

static int test_writer_add_value(void) {
    const char *source = "{\"a\":[1,2.5,{\"b\":null}],\"c\":\"x\"}";
    ca_json *json = must_parse(source, 0, NULL);
    ca_error err;
    ca_error_init(&err);

    cx_string *text = NULL;
    ca_json_writer *writer = NULL;
    CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, NULL, &err));
    CA_CHECK_EQ(CA_OK,
                ca_json_writer_add_value(writer, NULL, ca_json_root(json), &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_finish(writer, &text, &err));
    ca_json_writer_destroy(&writer);
    CA_CHECK(text != NULL);
    if (text) {
        CA_CHECK_EQ(0, strcmp(cx_string_cstr(text), source));
    }
    cx_string_destroy(&text);
    ca_json_destroy(&json);

    /* A NULL value is written as JSON null. */
    CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, NULL, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_add_value(writer, NULL, NULL, &err));
    CA_CHECK_EQ(CA_OK, ca_json_writer_finish(writer, &text, &err));
    CA_CHECK(text != NULL && strcmp(cx_string_cstr(text), "null") == 0);
    cx_string_destroy(&text);
    ca_json_writer_destroy(&writer);

    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_json_writer_add_value(NULL, NULL, NULL, &err));
    return ca_test_finish("json.writer_add_value");
}

int main(void) {
    int failures = 0;

    failures += test_scalars();
    failures += test_arrays();
    failures += test_objects();
    failures += test_object_keys();
    failures += test_strings();
    failures += test_numbers();
    failures += test_explicit_length();
    failures += test_invalid();
    failures += test_null_handles();
    failures += test_serialize();
    failures += test_writer();
    failures += test_writer_add_value();
    failures += test_allocator();
    failures += test_parse_file();
    failures += test_input_limit();
    failures += test_depth_limit();
    failures += test_container_limit();
    failures += test_parse_file_unicode();

    return failures;
}
