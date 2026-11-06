#include <cstdx/base64.h>

#include <stdint.h>

#include <cstdx/checked.h>

#include "bytes_internal.h"
#include "string_internal.h"

static constexpr char base64_alphabet[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/',
};

static bool base64_is_space(char value) {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' ||
           value == '\v' || value == '\f';
}

static int base64_value(char value) {
    if (value >= 'A' && value <= 'Z') {
        return value - 'A';
    }

    if (value >= 'a' && value <= 'z') {
        return value - 'a' + 26;
    }

    if (value >= '0' && value <= '9') {
        return value - '0' + 52;
    }

    if (value == '+') {
        return 62;
    }

    if (value == '/') {
        return 63;
    }

    return -1;
}

cx_status cx_base64_encoded_length(size_t input_length, size_t *out) {
    if (out == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    size_t padded;

    if (cx_add_overflow(input_length, 2, &padded)) {
        return CX_OVERFLOW;
    }

    size_t groups = padded / 3;
    size_t encoded;

    if (cx_mul_overflow(groups, 4, &encoded)) {
        return CX_OVERFLOW;
    }

    *out = encoded;
    return CX_OK;
}

cx_status cx_base64_encode(cx_slice input, cx_string **out,
                           const cx_allocator *allocator) {
    if (out == nullptr || (input.data == nullptr && input.length != 0) ||
        !cx_allocator_is_valid(allocator)) {
        return CX_INVALID_ARGUMENT;
    }

    *out = nullptr;

    cx_string *encoded = nullptr;
    auto status = cx_string_init(&encoded, allocator);

    if (status != CX_OK) {
        return status;
    }

    size_t encoded_length;

    status = cx_base64_encoded_length(input.length, &encoded_length);

    if (status != CX_OK) {
        cx_string_destroy(&encoded);
        return status;
    }

    status = cx_string_resize(encoded, encoded_length);

    if (status != CX_OK) {
        cx_string_destroy(&encoded);
        return status;
    }

    auto data = (const uint8_t *)input.data;
    auto text = encoded->data;
    size_t in = 0;
    size_t position = 0;

    while (in + 3 <= input.length) {
        auto triple = ((uint32_t)data[in] << 16) |
                      ((uint32_t)data[in + 1] << 8) | (uint32_t)data[in + 2];

        text[position] = base64_alphabet[(triple >> 18) & 0x3F];
        text[position + 1] = base64_alphabet[(triple >> 12) & 0x3F];
        text[position + 2] = base64_alphabet[(triple >> 6) & 0x3F];
        text[position + 3] = base64_alphabet[triple & 0x3F];

        in += 3;
        position += 4;
    }

    auto remaining = input.length - in;

    if (remaining == 1) {
        auto triple = (uint32_t)data[in] << 16;

        text[position] = base64_alphabet[(triple >> 18) & 0x3F];
        text[position + 1] = base64_alphabet[(triple >> 12) & 0x3F];
        text[position + 2] = '=';
        text[position + 3] = '=';
    } else if (remaining == 2) {
        auto triple =
            ((uint32_t)data[in] << 16) | ((uint32_t)data[in + 1] << 8);

        text[position] = base64_alphabet[(triple >> 18) & 0x3F];
        text[position + 1] = base64_alphabet[(triple >> 12) & 0x3F];
        text[position + 2] = base64_alphabet[(triple >> 6) & 0x3F];
        text[position + 3] = '=';
    }

    *out = encoded;
    return CX_OK;
}

cx_status cx_base64_decode(cx_string_view input, cx_bytes **out,
                           const cx_allocator *allocator) {
    if (out == nullptr || (input.data == nullptr && input.length != 0) ||
        !cx_allocator_is_valid(allocator)) {
        return CX_INVALID_ARGUMENT;
    }

    *out = nullptr;

    cx_bytes *result = nullptr;
    auto status = cx_bytes_init(&result, allocator);

    if (status != CX_OK) {
        return status;
    }

    size_t data_chars = 0;
    size_t padding = 0;
    bool seen_padding = false;

    for (size_t i = 0; i < input.length; ++i) {
        char value = input.data[i];

        if (base64_is_space(value)) {
            continue;
        }

        if (value == '=') {
            seen_padding = true;

            if (++padding > 2) {
                cx_bytes_destroy(&result);
                return CX_INVALID_ARGUMENT;
            }

            continue;
        }

        if (seen_padding || base64_value(value) < 0) {
            cx_bytes_destroy(&result);
            return CX_INVALID_ARGUMENT;
        }

        ++data_chars;
    }

    auto remainder = data_chars % 4;
    size_t decoded_length;

    if (padding == 0) {
        if (remainder == 1) {
            cx_bytes_destroy(&result);
            return CX_INVALID_ARGUMENT;
        }

        decoded_length = (data_chars / 4) * 3 +
                         (remainder == 2 ? 1 : remainder == 3 ? 2 : 0);
    } else if (padding == 1) {
        if (remainder != 3) {
            cx_bytes_destroy(&result);
            return CX_INVALID_ARGUMENT;
        }

        decoded_length = (data_chars / 4) * 3 + 2;
    } else {
        if (remainder != 2) {
            cx_bytes_destroy(&result);
            return CX_INVALID_ARGUMENT;
        }

        decoded_length = (data_chars / 4) * 3 + 1;
    }

    status = cx_bytes_resize(result, decoded_length);

    if (status != CX_OK) {
        cx_bytes_destroy(&result);
        return status;
    }

    size_t out_index = 0;
    uint32_t accumulator = 0;
    size_t group_length = 0;

    for (size_t i = 0; i < input.length; ++i) {
        char value = input.data[i];

        if (base64_is_space(value)) {
            continue;
        }

        if (value == '=') {
            break;
        }

        accumulator = (accumulator << 6) | (uint32_t)base64_value(value);
        ++group_length;

        if (group_length == 4) {
            result->data[out_index++] = (uint8_t)(accumulator >> 16);
            result->data[out_index++] = (uint8_t)(accumulator >> 8);
            result->data[out_index++] = (uint8_t)accumulator;
            accumulator = 0;
            group_length = 0;
        }
    }

    if (group_length == 2) {
        if ((accumulator & 0x0F) != 0) {
            cx_bytes_destroy(&result);
            return CX_INVALID_ARGUMENT;
        }

        result->data[out_index++] = (uint8_t)(accumulator >> 4);
    } else if (group_length == 3) {
        if ((accumulator & 0x03) != 0) {
            cx_bytes_destroy(&result);
            return CX_INVALID_ARGUMENT;
        }

        result->data[out_index++] = (uint8_t)(accumulator >> 10);
        result->data[out_index++] = (uint8_t)(accumulator >> 2);
    } else if (group_length == 1) {
        cx_bytes_destroy(&result);
        return CX_INVALID_ARGUMENT;
    }

    if (out_index != decoded_length) {
        cx_bytes_destroy(&result);
        return CX_INTERNAL;
    }

    *out = result;
    return CX_OK;
}

cx_status cx_base64_decode_cstr(const char *input, cx_bytes **out,
                                const cx_allocator *allocator) {
    if (input == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    return cx_base64_decode(cx_string_view_from_cstr(input), out, allocator);
}
