#include <cstdx/string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * Fuzz cx_string construction, copying, aliasing append, and erasure.
 */
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    cx_string* string = nullptr;

    if (cx_string_from_view(&string, (cx_string_view){(const char*)data, size}, nullptr) != CX_OK) {
        return 0;
    }

    if (cx_string_length(string) != size) {
        abort();
    }

    if (memcmp(cx_string_cstr(string), data, size) != 0) {
        abort();
    }

    if (cx_string_cstr(string)[size] != '\0') {
        abort();
    }

    cx_string* copy = nullptr;

    if (cx_string_copy(&copy, string) == CX_OK) {
        if (cx_string_compare(string, copy) != CX_STRING_EQUAL) {
            abort();
        }

        if (cx_string_append(string, copy) != CX_OK) {
            abort();
        }

        if (cx_string_length(string) != size * 2) {
            abort();
        }

        cx_string_destroy(&copy);
    }

    const size_t index = size == 0 ? 0 : data[0] % (size + 1);
    const size_t count = size == 0 ? 0 : data[size - 1] % (size + 1);

    (void)cx_string_erase(string, index, count);
    cx_string_clear(string);

    if (!cx_string_empty(string)) {
        abort();
    }

    cx_string_destroy(&string);

    if (string != nullptr) {
        abort();
    }

    return 0;
}
