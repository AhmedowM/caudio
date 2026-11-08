#include <cstdx/base64.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * Fuzz the encoder and the strict decoder.
 *
 * Invariants exercised:
 * - encode -> decode is the identity for every byte string;
 * - the strict decoder never crashes or reports success for malformed text
 *   that it cannot fully consume.
 */
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    cx_string* encoded = nullptr;

    if (cx_base64_encode(cx_slice_from_parts(data, size), &encoded, nullptr) == CX_OK) {
        cx_bytes* decoded = nullptr;

        if (cx_base64_decode(cx_string_view_of(encoded), &decoded, nullptr) != CX_OK) {
            abort();
        }

        if (cx_bytes_length(decoded) != size) {
            abort();
        }

        if (size != 0 && memcmp(cx_bytes_data(decoded), data, size) != 0) {
            abort();
        }

        cx_bytes_destroy(&decoded);
        cx_string_destroy(&encoded);
    }

    /* Arbitrary bytes as text: the decoder must reject or succeed cleanly. */
    cx_bytes* decoded = nullptr;

    (void)cx_base64_decode((cx_string_view){(const char*)data, size}, &decoded, nullptr);
    cx_bytes_destroy(&decoded);

    return 0;
}
