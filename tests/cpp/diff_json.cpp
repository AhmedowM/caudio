#include <caudio/runtime/json.h>

#include <cstdx/allocator.h>

#include <yyjson.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

/**
 * @file tests/cpp/diff_json.cpp
 * @brief Differential test for the ca_json wrapper.
 *
 * ca_json is a thin wrapper over yyjson, so the parser itself is not the thing
 * under test here. This test parses the same documents with ca_json and with
 * yyjson directly, then compares every observable value through the public
 * ca_json API. It also round-trips both serializers in both directions.
 *
 * The test is dependency-free (no GoogleTest) so it can be enabled with
 * -DCAUDIO_BUILD_CPP_TESTS=ON without fetching any packages. The oracle is the
 * vendored yyjson build.
 */

namespace {

int failures = 0;

void check_fail(const char *file, int line, const std::string &path,
                const char *expr) {
    std::fprintf(stderr, "%s:%d: %s: check failed: %s\n", file, line,
                 path.empty() ? "<root>" : path.c_str(), expr);
    ++failures;
}

ca_json_type expected_type(const yyjson_val *val) {
    yyjson_type type = yyjson_get_type(val);
    yyjson_subtype subtype = yyjson_get_subtype(val);

    switch (type) {
    case YYJSON_TYPE_NULL:
        return CA_JSON_NULL;
    case YYJSON_TYPE_BOOL:
        return subtype == YYJSON_SUBTYPE_TRUE ? CA_JSON_TRUE : CA_JSON_FALSE;
    case YYJSON_TYPE_NUM:
        switch (subtype) {
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

void compare_value(const yyjson_val *ref, const ca_json_val *val,
                   const std::string &path) {
#define CHECK_AT(cond)                                                       \
    do {                                                                     \
        if (!(cond)) {                                                       \
            check_fail(__FILE__, __LINE__, path, #cond);                     \
        }                                                                    \
    } while (0)

    ca_json_type type = expected_type(ref);
    CHECK_AT(ca_json_type_of(val) == type);

    switch (type) {
    case CA_JSON_NULL:
    case CA_JSON_TRUE:
    case CA_JSON_FALSE:
        break;

    case CA_JSON_INT:
        CHECK_AT(ca_json_get_int(val) == yyjson_get_sint(ref));
        CHECK_AT(ca_json_get_uint(val) == 0);
        CHECK_AT(ca_json_get_double(val) == 0.0);
        break;

    case CA_JSON_UINT:
        CHECK_AT(ca_json_get_uint(val) == yyjson_get_uint(ref));
        CHECK_AT(ca_json_get_int(val) == 0);
        CHECK_AT(ca_json_get_double(val) == 0.0);
        break;

    case CA_JSON_DOUBLE:
        CHECK_AT(ca_json_get_double(val) == yyjson_get_real(ref));
        CHECK_AT(ca_json_get_int(val) == 0);
        CHECK_AT(ca_json_get_uint(val) == 0);
        break;

    case CA_JSON_STRING: {
        size_t ref_len = yyjson_get_len(ref);
        size_t val_len = 0;
        const char *str = ca_json_get_string(val, &val_len);

        CHECK_AT(str != nullptr);
        CHECK_AT(val_len == ref_len);
        if (str != nullptr && val_len == ref_len) {
            CHECK_AT(std::memcmp(str, yyjson_get_str(ref), ref_len) == 0);
        }
        break;
    }

    case CA_JSON_ARRAY: {
        size_t count = yyjson_arr_size(ref);
        CHECK_AT(ca_json_array_size(val) == count);

        for (size_t i = 0; i < count; ++i) {
            compare_value(yyjson_arr_get(ref, i), ca_json_array_get(val, i),
                          path + "[" + std::to_string(i) + "]");
        }
        break;
    }

    case CA_JSON_OBJECT: {
        size_t count = yyjson_obj_size(ref);
        CHECK_AT(ca_json_object_size(val) == count);

        yyjson_obj_iter iter = yyjson_obj_iter_with(ref);
        yyjson_val *key = nullptr;
        while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
            const char *name = yyjson_get_str(key);
            const ca_json_val *child = ca_json_object_get(val, name);

            CHECK_AT(child != nullptr);
            if (child != nullptr) {
                compare_value(yyjson_obj_iter_get_val(key), child,
                              path + "." + name);
            }
        }
        break;
    }
    }

#undef CHECK_AT
}

bool parse_caudio(const std::string &text, ca_json **out) {
    ca_error err;
    ca_error_init(&err);

    ca_json *json = nullptr;
    ca_status status = ca_json_parse(&json, text.data(), text.size(), nullptr,
                                     &err);
    if (status != CA_OK) {
        std::fprintf(stderr, "ca_json_parse failed: %s\n", err.message);
        ++failures;
        return false;
    }

    *out = json;
    return true;
}

yyjson_doc *parse_yyjson(const std::string &text) {
    yyjson_read_err err = {};
    yyjson_doc *doc = yyjson_read_opts(const_cast<char *>(text.data()),
                                       text.size(), 0, nullptr, &err);
    if (doc == nullptr) {
        std::fprintf(stderr, "yyjson_read failed: %s\n",
                     err.msg != nullptr ? err.msg : "unknown");
        ++failures;
    }
    return doc;
}

void check_document(const std::string &text) {
    ca_json *json = nullptr;
    if (!parse_caudio(text, &json)) {
        return;
    }

    yyjson_doc *ref = parse_yyjson(text);
    if (ref == nullptr) {
        ca_json_destroy(&json);
        return;
    }

    compare_value(yyjson_doc_get_root(ref), ca_json_root(json), "");

    /* ca_json -> yyjson, compact and pretty. */
    for (int pretty = 0; pretty <= 1; ++pretty) {
        ca_error err;
        ca_error_init(&err);
        size_t len = 0;
        char *serialized = ca_json_serialize(json, pretty, &len, nullptr, &err);
        if (serialized == nullptr) {
            std::fprintf(stderr, "ca_json_serialize(pretty=%d) failed: %s\n",
                         pretty, err.message);
            ++failures;
            continue;
        }

        yyjson_read_err rerr = {};
        yyjson_doc *round = yyjson_read_opts(serialized, len, 0, nullptr,
                                             &rerr);
        if (round == nullptr) {
            std::fprintf(stderr, "re-parsing ca_json output failed: %s\n",
                         rerr.msg != nullptr ? rerr.msg : "unknown");
            ++failures;
        } else {
            compare_value(yyjson_doc_get_root(round), ca_json_root(json), "");
            yyjson_doc_free(round);
        }

        cx_allocator_free(cx_allocator_default(), serialized, len + 1, 0);
    }

    /* yyjson -> ca_json. */
    yyjson_write_err werr = {};
    size_t out_len = 0;
    char *out = yyjson_write_opts(ref, 0, nullptr, &out_len, &werr);
    if (out == nullptr) {
        std::fprintf(stderr, "yyjson_write failed: %s\n",
                     werr.msg != nullptr ? werr.msg : "unknown");
        ++failures;
    } else {
        ca_json *round = nullptr;
        ca_error err;
        ca_error_init(&err);
        ca_json *round_out = nullptr;
        if (ca_json_parse(&round_out, out, out_len, nullptr, &err) == CA_OK) {
            round = round_out;
            compare_value(yyjson_doc_get_root(ref), ca_json_root(round), "");
            ca_json_destroy(&round);
        } else {
            std::fprintf(stderr, "re-parsing yyjson output failed: %s\n",
                         err.message);
            ++failures;
        }
        std::free(out);
    }

    yyjson_doc_free(ref);
    ca_json_destroy(&json);
}

void check_invalid(const std::string &text) {
    ca_error err;
    ca_error_init(&err);

    ca_json *json = nullptr;
    ca_status status = ca_json_parse(&json, text.data(), text.size(), nullptr,
                                     &err);
    if (status == CA_OK) {
        std::fprintf(stderr, "ca_json accepted invalid JSON: %s\n",
                     text.c_str());
        ++failures;
        ca_json_destroy(&json);
    }

    yyjson_read_err rerr = {};
    yyjson_doc *ref = yyjson_read_opts(const_cast<char *>(text.data()),
                                       text.size(), 0, nullptr, &rerr);
    if (ref != nullptr) {
        std::fprintf(stderr, "yyjson accepted invalid JSON: %s\n",
                     text.c_str());
        ++failures;
        yyjson_doc_free(ref);
    }
}

} // namespace

int main() {
    const std::vector<std::string> documents = {
        "null",
        "true",
        "false",
        "0",
        "42",
        "-42",
        "9223372036854775807",
        "18446744073709551615",
        "-9223372036854775808",
        "1.5",
        "-2.25e10",
        "0.0",
        "\"\"",
        "\"hello\"",
        "\"caf\\u00e9 \\u4e16\\u754c\"",
        "\"quote\\\" backslash\\\\ slash/ tab\\t newline\\n\"",
        "[]",
        "{}",
        "[1,2,3]",
        "{\"a\":1,\"b\":2}",
        "{\"nested\":{\"arr\":[true,false,null,0,1.5,\"x\"]}}",
        "[[[[[0]]]]]",
        "{\"empty_arr\":[],\"empty_obj\":{}}",
        "{\"unicode\":\"\xE4\xB8\x96\xE7\x95\x8C\"}",
        "{\"escaped\":\"line\\nfeed \\u0001\"}",
        "{\"long\":[0,1,2,3,4,5,6,7,8,9],\"neg\":[-1,-2,-3]}",
    };

    for (const std::string &text : documents) {
        check_document(text);
    }

    const std::vector<std::string> invalid = {
        "",
        "{",
        "[1,]",
        "[1] junk",
        "nul",
        "\"unterminated",
        "{,}",
        "{} {}",
        "01",
        "{\"a\":}",
        "[1,,2]",
        "tru",
        "falsey",
    };

    for (const std::string &text : invalid) {
        check_invalid(text);
    }

    if (failures != 0) {
        std::fprintf(stderr, "caudio.diff.json: %d failure(s)\n", failures);
        return 1;
    }

    std::printf("caudio.diff.json: ok\n");
    return 0;
}
