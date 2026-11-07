#include <caudio/runtime/json.h>

#include <cstdx/allocator.h>
#include <cstdx/allocator_counted.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ca_test.h"

static const char *JSON_FILE = "caudio_test_json_file.json";

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
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_json_parse(&json, "{", 0, &alloc, &err));
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

    json = (ca_json *)(uintptr_t)1;
    CA_CHECK_EQ(CA_IO_ERROR,
                ca_json_parse_file(&json, "caudio_test_json_missing.json", NULL,
                                   &err));
    CA_CHECK(json == NULL);

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

int main(void) {
    int failures = 0;

    failures += test_scalars();
    failures += test_arrays();
    failures += test_objects();
    failures += test_strings();
    failures += test_numbers();
    failures += test_explicit_length();
    failures += test_invalid();
    failures += test_null_handles();
    failures += test_serialize();
    failures += test_allocator();
    failures += test_parse_file();

    return failures;
}
