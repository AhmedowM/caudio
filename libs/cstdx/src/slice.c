#include <cstdx/checked.h>
#include <cstdx/slice.h>
#include <string.h>

cx_slice cx_slice_from_parts(const void* data, size_t length) {
    if (data == nullptr) {
        length = 0;
    }

    return (cx_slice){
        .data = data,
        .length = length,
    };
}

cx_status cx_slice_sub(cx_slice slice, size_t offset, size_t length, cx_slice* out) {
    if (out == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (offset > slice.length || length > slice.length - offset) {
        return CX_OUT_OF_RANGE;
    }

    auto base = (const uint8_t*)slice.data;

    *out = (cx_slice){
        .data = base != nullptr ? base + offset : nullptr,
        .length = length,
    };

    return CX_OK;
}

const uint8_t* cx_slice_at(const cx_slice* slice, size_t index) {
    if (slice == nullptr || slice->data == nullptr || index >= slice->length) {
        return nullptr;
    }

    return (const uint8_t*)slice->data + index;
}

cx_status cx_slice_get(const cx_slice* slice, size_t index, uint8_t* value) {
    if (slice == nullptr || value == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (index >= slice->length) {
        return CX_OUT_OF_RANGE;
    }

    *value = ((const uint8_t*)slice->data)[index];
    return CX_OK;
}

bool cx_slice_equals(cx_slice left, cx_slice right) {
    if (left.length != right.length) {
        return false;
    }

    if (left.length == 0) {
        return true;
    }

    if (left.data == nullptr || right.data == nullptr) {
        return false;
    }

    return memcmp(left.data, right.data, left.length) == 0;
}

bool cx_slice_starts_with(cx_slice slice, cx_slice prefix) {
    if (prefix.length > slice.length) {
        return false;
    }

    if (prefix.length == 0) {
        return true;
    }

    if (slice.data == nullptr || prefix.data == nullptr) {
        return false;
    }

    return memcmp(slice.data, prefix.data, prefix.length) == 0;
}

bool cx_slice_ends_with(cx_slice slice, cx_slice suffix) {
    if (suffix.length > slice.length) {
        return false;
    }

    if (suffix.length == 0) {
        return true;
    }

    if (slice.data == nullptr || suffix.data == nullptr) {
        return false;
    }

    auto base = (const uint8_t*)slice.data;

    return memcmp(base + slice.length - suffix.length, suffix.data, suffix.length) == 0;
}

cx_status cx_slice_find(cx_slice haystack, cx_slice needle, size_t* out) {
    if (out == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (needle.length == 0) {
        *out = 0;
        return CX_OK;
    }

    if (needle.length > haystack.length || haystack.data == nullptr || needle.data == nullptr) {
        return CX_NOT_FOUND;
    }

    auto haystack_bytes = (const uint8_t*)haystack.data;
    auto needle_bytes = (const uint8_t*)needle.data;
    auto limit = haystack.length - needle.length;

    for (size_t i = 0; i <= limit; ++i) {
        if (haystack_bytes[i] == needle_bytes[0] &&
            memcmp(haystack_bytes + i, needle_bytes, needle.length) == 0) {
            *out = i;
            return CX_OK;
        }
    }

    return CX_NOT_FOUND;
}
