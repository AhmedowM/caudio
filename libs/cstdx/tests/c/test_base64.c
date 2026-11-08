#include <cstdx/allocator_counted.h>
#include <cstdx/base64.h>
#include <string.h>

#include "cx_test.h"

static bool encode_matches(const void* data, size_t length, const char* expected) {
    cx_string* encoded = nullptr;
    auto status = cx_base64_encode(cx_slice_from_parts(data, length), &encoded, nullptr);

    if (status != CX_OK) {
        return false;
    }

    bool matches = cx_string_length(encoded) == strlen(expected) &&
                   strcmp(cx_string_cstr(encoded), expected) == 0;

    cx_string_destroy(&encoded);
    return matches;
}

static bool decode_matches(const char* text, const void* expected, size_t length) {
    cx_bytes* decoded = nullptr;
    auto status = cx_base64_decode_cstr(text, &decoded, nullptr);

    if (status != CX_OK) {
        return false;
    }

    bool matches = cx_bytes_length(decoded) == length &&
                   (length == 0 || memcmp(cx_bytes_data(decoded), expected, length) == 0);

    cx_bytes_destroy(&decoded);
    return matches;
}

static bool decode_fails(const char* text) {
    cx_bytes* decoded = nullptr;
    auto status = cx_base64_decode_cstr(text, &decoded, nullptr);

    if (status == CX_OK) {
        cx_bytes_destroy(&decoded);
        return false;
    }

    return true;
}

static int test_encoding(void) {
    CX_CHECK(encode_matches(nullptr, 0, ""));
    CX_CHECK(encode_matches("f", 1, "Zg=="));
    CX_CHECK(encode_matches("fo", 2, "Zm8="));
    CX_CHECK(encode_matches("foo", 3, "Zm9v"));
    CX_CHECK(encode_matches("foob", 4, "Zm9vYg=="));
    CX_CHECK(encode_matches("fooba", 5, "Zm9vYmE="));
    CX_CHECK(encode_matches("foobar", 6, "Zm9vYmFy"));

    uint8_t binary[] = {0x00, 0xFF, 0x10};

    CX_CHECK(encode_matches(binary, sizeof(binary), "AP8Q"));

    return cx_test_finish("base64.encoding");
}

static int test_decoding(void) {
    CX_CHECK(decode_matches("", "", 0));
    CX_CHECK(decode_matches("Zg==", "f", 1));
    CX_CHECK(decode_matches("Zg", "f", 1));
    CX_CHECK(decode_matches("Zm8=", "fo", 2));
    CX_CHECK(decode_matches("Zm8", "fo", 2));
    CX_CHECK(decode_matches("Zm9v", "foo", 3));
    CX_CHECK(decode_matches("Zm9vYg==", "foob", 4));
    CX_CHECK(decode_matches("Zm9vYmE=", "fooba", 5));
    CX_CHECK(decode_matches("Zm9vYmFy", "foobar", 6));
    CX_CHECK(decode_matches(" Zm9v\nYmFy\t", "foobar", 6));

    uint8_t expected[] = {0x00, 0xFF, 0x10};

    CX_CHECK(decode_matches("AP8Q", expected, sizeof(expected)));

    return cx_test_finish("base64.decoding");
}

static int test_invalid(void) {
    CX_CHECK(decode_fails("A"));
    CX_CHECK(decode_fails("Zg="));
    CX_CHECK(decode_fails("Zg==="));
    CX_CHECK(decode_fails("Z=g="));
    CX_CHECK(decode_fails("Zm9v!"));
    CX_CHECK(decode_fails("Zg==Zg=="));
    CX_CHECK(decode_fails("Zh=="));
    CX_CHECK(decode_fails("Zm9="));
    CX_CHECK(decode_fails("===="));

    cx_bytes* decoded = nullptr;

    CX_CHECK(cx_base64_decode(cx_string_view_from_cstr("Zg=="), nullptr, nullptr) ==
             CX_INVALID_ARGUMENT);
    CX_CHECK(cx_base64_decode(cx_string_view_from_cstr("Zg=="), &decoded, nullptr) == CX_OK);
    CX_CHECK(decoded != nullptr);
    cx_bytes_destroy(&decoded);
    CX_CHECK(cx_base64_decode_cstr(nullptr, &decoded, nullptr) == CX_INVALID_ARGUMENT);

    return cx_test_finish("base64.invalid");
}

static int test_length(void) {
    size_t length = 0;

    CX_CHECK(cx_base64_encoded_length(0, &length) == CX_OK);
    CX_CHECK_EQ(0, length);
    CX_CHECK(cx_base64_encoded_length(1, &length) == CX_OK);
    CX_CHECK_EQ(4, length);
    CX_CHECK(cx_base64_encoded_length(2, &length) == CX_OK);
    CX_CHECK_EQ(4, length);
    CX_CHECK(cx_base64_encoded_length(3, &length) == CX_OK);
    CX_CHECK_EQ(4, length);
    CX_CHECK(cx_base64_encoded_length(100, &length) == CX_OK);
    CX_CHECK_EQ(136, length);
    CX_CHECK(cx_base64_encoded_length(SIZE_MAX, &length) == CX_OVERFLOW);
    CX_CHECK(cx_base64_encoded_length(1, nullptr) == CX_INVALID_ARGUMENT);

    return cx_test_finish("base64.length");
}

static int test_counted_allocator(void) {
    cx_allocator_counted counted;

    CX_CHECK(cx_allocator_counted_init(&counted, nullptr) == CX_OK);

    auto allocator = cx_allocator_counted_as_allocator(&counted);
    cx_string* encoded = nullptr;
    cx_bytes* decoded = nullptr;

    CX_CHECK(cx_base64_encode(cx_slice_from_parts("hello world", 11), &encoded, &allocator) ==
             CX_OK);
    CX_CHECK(cx_string_equals_cstr(encoded, "aGVsbG8gd29ybGQ="));

    CX_CHECK(cx_base64_decode(cx_string_view_of(encoded), &decoded, &allocator) == CX_OK);
    CX_CHECK_EQ(11, cx_bytes_length(decoded));
    CX_CHECK(memcmp(cx_bytes_data(decoded), "hello world", 11) == 0);

    cx_string_destroy(&encoded);
    cx_bytes_destroy(&decoded);
    CX_CHECK(!cx_allocator_counted_has_leaks(&counted));

    cx_allocator_counted_destroy(&counted);

    return cx_test_finish("base64.counted_allocator");
}

int main(void) {
    int failures = 0;

    failures += test_encoding();
    failures += test_decoding();
    failures += test_invalid();
    failures += test_length();
    failures += test_counted_allocator();

    return failures == 0 ? 0 : 1;
}
