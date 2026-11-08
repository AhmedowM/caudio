#include <cstdx/vector.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * Fuzz cx_vector push/access/pop for a trivially copyable element type.
 */
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    cx_vector* vector = nullptr;

    if (cx_vector_init(&vector, sizeof(int), alignof(int), nullptr, nullptr) != CX_OK) {
        return 0;
    }

    const size_t count = size / sizeof(int);

    for (size_t i = 0; i < count; ++i) {
        int value = 0;

        memcpy(&value, data + i * sizeof(int), sizeof(value));

        if (cx_vector_push(vector, &value) != CX_OK) {
            abort();
        }
    }

    if (cx_vector_length(vector) != count) {
        abort();
    }

    for (size_t i = 0; i < count; ++i) {
        int expected = 0;

        memcpy(&expected, data + i * sizeof(int), sizeof(expected));

        if (cx_vector_at(vector, int, i) != expected) {
            abort();
        }
    }

    if (cx_vector_as_slice(vector).length != count * sizeof(int)) {
        abort();
    }

    while (!cx_vector_empty(vector)) {
        if (cx_vector_pop(vector, nullptr) != CX_OK) {
            abort();
        }
    }

    cx_vector_destroy(&vector);

    if (vector != nullptr) {
        abort();
    }

    return 0;
}
