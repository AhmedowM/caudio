#include <cstdx/checked.h>
#include <stdint.h>
#include <string.h>

#include "bytes_internal.h"

static const cx_allocator* bytes_allocator(const cx_bytes* bytes) {
    return bytes->allocator != nullptr ? bytes->allocator : cx_allocator_default();
}

static bool bytes_contains_range(const cx_bytes* bytes, const void* data, size_t length) {
    if (bytes->data == nullptr || data == nullptr) {
        return false;
    }

    auto base = (uintptr_t)bytes->data;
    auto end = base + (uintptr_t)bytes->capacity;
    auto start = (uintptr_t)data;

    if (start < base || start > end) {
        return false;
    }

    return (size_t)(end - start) >= length;
}

/**
 * Copy a range out of the destination buffer when it aliases it.
 *
 * Growing the destination may move its buffer, so an aliased source must be
 * detached first.
 */
static cx_status bytes_stage(cx_bytes* bytes, const void* data, size_t length, const void** source,
                             void** staged) {
    *staged = nullptr;
    *source = data;

    if (length == 0 || !bytes_contains_range(bytes, data, length)) {
        return CX_OK;
    }

    auto copy = cx_allocator_alloc(bytes_allocator(bytes), length, 1);

    if (copy == nullptr) {
        return CX_OUT_OF_MEMORY;
    }

    memcpy(copy, data, length);
    *source = copy;
    *staged = copy;
    return CX_OK;
}

static void bytes_unstage(cx_bytes* bytes, void* staged, size_t length) {
    if (staged != nullptr) {
        cx_allocator_free(bytes_allocator(bytes), staged, length, 1);
    }
}

static cx_status bytes_reserve_impl(cx_bytes* bytes, size_t min_capacity) {
    if (bytes == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (min_capacity <= bytes->capacity) {
        return CX_OK;
    }

    auto capacity = bytes->capacity;

    if (capacity == 0) {
        capacity = 8;
    }

    while (capacity < min_capacity) {
        size_t grown;

        if (cx_mul_overflow(capacity, 2, &grown)) {
            capacity = min_capacity;
            break;
        }

        capacity = grown;
    }

    auto allocator = bytes_allocator(bytes);
    void* data;

    if (bytes->data == nullptr) {
        data = cx_allocator_alloc(allocator, capacity, 1);
    } else {
        data = cx_allocator_realloc(allocator, bytes->data, bytes->capacity, capacity, 1);
    }

    if (data == nullptr) {
        return CX_OUT_OF_MEMORY;
    }

    bytes->data = (uint8_t*)data;
    bytes->capacity = capacity;
    return CX_OK;
}

cx_status cx_impl_bytes_init(cx_bytes* bytes, const cx_allocator* allocator) {
    if (bytes == nullptr || !cx_allocator_is_valid(allocator)) {
        return CX_INVALID_ARGUMENT;
    }

    *bytes = (cx_bytes){
        .allocator = allocator != nullptr ? allocator : cx_allocator_default(),
    };
    return CX_OK;
}

void cx_impl_bytes_destroy(cx_bytes* bytes) {
    if (bytes == nullptr) {
        return;
    }

    auto allocator = bytes_allocator(bytes);

    if (bytes->data != nullptr) {
        cx_allocator_free(allocator, bytes->data, bytes->capacity, 1);
    }

    *bytes = (cx_bytes){
        .allocator = allocator,
    };
}

cx_status cx_bytes_init(cx_bytes** out, const cx_allocator* allocator) {
    if (out == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    *out = nullptr;

    cx_bytes staging;
    auto status = cx_impl_bytes_init(&staging, allocator);

    if (status != CX_OK) {
        return status;
    }

    auto resolved = bytes_allocator(&staging);
    auto bytes = (cx_bytes*)cx_allocator_alloc(resolved, sizeof(cx_bytes), alignof(cx_bytes));

    if (bytes == nullptr) {
        return CX_OUT_OF_MEMORY;
    }

    *bytes = staging;
    *out = bytes;
    return CX_OK;
}

cx_status cx_bytes_from(cx_bytes** out, const void* data, size_t length,
                        const cx_allocator* allocator) {
    if (out == nullptr || (data == nullptr && length != 0)) {
        return CX_INVALID_ARGUMENT;
    }

    *out = nullptr;

    cx_bytes* bytes = nullptr;
    auto status = cx_bytes_init(&bytes, allocator);

    if (status != CX_OK) {
        return status;
    }

    status = cx_bytes_assign(bytes, data, length);

    if (status != CX_OK) {
        cx_bytes_destroy(&bytes);
        return status;
    }

    *out = bytes;
    return CX_OK;
}

cx_status cx_bytes_from_slice(cx_bytes** out, cx_slice slice, const cx_allocator* allocator) {
    return cx_bytes_from(out, slice.data, slice.length, allocator);
}

cx_status cx_bytes_copy(cx_bytes** out, const cx_bytes* src) {
    if (out == nullptr || src == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    *out = nullptr;
    return cx_bytes_from(out, src->data, src->length, src->allocator);
}

void cx_bytes_move(cx_bytes* dst, cx_bytes* src) {
    if (dst == nullptr || src == nullptr || dst == src) {
        return;
    }

    auto src_allocator = bytes_allocator(src);

    if (dst->data != nullptr) {
        cx_allocator_free(bytes_allocator(dst), dst->data, dst->capacity, 1);
    }

    *dst = *src;
    *src = (cx_bytes){
        .allocator = src_allocator,
    };
}

void cx_bytes_destroy(cx_bytes** out) {
    if (out == nullptr || *out == nullptr) {
        return;
    }

    auto bytes = *out;
    *out = nullptr;

    auto allocator = bytes_allocator(bytes);

    cx_impl_bytes_destroy(bytes);
    cx_allocator_free(allocator, bytes, sizeof(cx_bytes), alignof(cx_bytes));
}

const uint8_t* cx_bytes_at(const cx_bytes* bytes, size_t index) {
    if (bytes == nullptr || bytes->data == nullptr || index >= bytes->length) {
        return nullptr;
    }

    return bytes->data + index;
}

uint8_t* cx_bytes_at_mut(cx_bytes* bytes, size_t index) {
    if (bytes == nullptr || bytes->data == nullptr || index >= bytes->length) {
        return nullptr;
    }

    return bytes->data + index;
}

cx_status cx_bytes_get(const cx_bytes* bytes, size_t index, uint8_t* value) {
    if (bytes == nullptr || value == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (index >= bytes->length) {
        return CX_OUT_OF_RANGE;
    }

    *value = bytes->data[index];
    return CX_OK;
}

cx_status cx_bytes_set(cx_bytes* bytes, size_t index, uint8_t value) {
    if (bytes == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (index >= bytes->length) {
        return CX_OUT_OF_RANGE;
    }

    bytes->data[index] = value;
    return CX_OK;
}

cx_status cx_bytes_reserve(cx_bytes* bytes, size_t min_capacity) {
    return bytes_reserve_impl(bytes, min_capacity);
}

cx_status cx_bytes_resize(cx_bytes* bytes, size_t new_length) {
    if (bytes == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    auto status = bytes_reserve_impl(bytes, new_length);

    if (status != CX_OK) {
        return status;
    }

    if (new_length > bytes->length) {
        memset(bytes->data + bytes->length, 0, new_length - bytes->length);
    }

    bytes->length = new_length;
    return CX_OK;
}

cx_status cx_bytes_assign(cx_bytes* bytes, const void* data, size_t length) {
    if (bytes == nullptr || (data == nullptr && length != 0)) {
        return CX_INVALID_ARGUMENT;
    }

    const void* source;
    void* staged;
    auto status = bytes_stage(bytes, data, length, &source, &staged);

    if (status != CX_OK) {
        return status;
    }

    status = bytes_reserve_impl(bytes, length);

    if (status == CX_OK) {
        if (length != 0) {
            memcpy(bytes->data, source, length);
        }

        bytes->length = length;
    }

    bytes_unstage(bytes, staged, length);
    return status;
}

cx_status cx_bytes_assign_slice(cx_bytes* bytes, cx_slice slice) {
    return cx_bytes_assign(bytes, slice.data, slice.length);
}

void cx_bytes_clear(cx_bytes* bytes) {
    if (bytes == nullptr) {
        return;
    }

    bytes->length = 0;
}

cx_status cx_bytes_append(cx_bytes* bytes, const void* data, size_t length) {
    if (bytes == nullptr || (data == nullptr && length != 0)) {
        return CX_INVALID_ARGUMENT;
    }

    if (length == 0) {
        return CX_OK;
    }

    size_t new_length;

    if (cx_add_overflow(bytes->length, length, &new_length)) {
        return CX_OVERFLOW;
    }

    const void* source;
    void* staged;
    auto status = bytes_stage(bytes, data, length, &source, &staged);

    if (status != CX_OK) {
        return status;
    }

    status = bytes_reserve_impl(bytes, new_length);

    if (status == CX_OK) {
        memcpy(bytes->data + bytes->length, source, length);
        bytes->length = new_length;
    }

    bytes_unstage(bytes, staged, length);
    return status;
}

cx_status cx_bytes_append_slice(cx_bytes* bytes, cx_slice slice) {
    return cx_bytes_append(bytes, slice.data, slice.length);
}

cx_status cx_bytes_push_back(cx_bytes* bytes, uint8_t value) {
    if (bytes == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    size_t new_length;

    if (cx_add_overflow(bytes->length, 1, &new_length)) {
        return CX_OVERFLOW;
    }

    auto status = bytes_reserve_impl(bytes, new_length);

    if (status != CX_OK) {
        return status;
    }

    bytes->data[bytes->length] = value;
    bytes->length = new_length;
    return CX_OK;
}

cx_status cx_bytes_pop_back(cx_bytes* bytes, uint8_t* value) {
    if (bytes == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (bytes->length == 0) {
        return CX_OUT_OF_RANGE;
    }

    bytes->length--;

    if (value != nullptr) {
        *value = bytes->data[bytes->length];
    }

    return CX_OK;
}

cx_status cx_bytes_insert(cx_bytes* bytes, size_t index, const void* data, size_t length) {
    if (bytes == nullptr || (data == nullptr && length != 0)) {
        return CX_INVALID_ARGUMENT;
    }

    if (index > bytes->length) {
        return CX_OUT_OF_RANGE;
    }

    if (length == 0) {
        return CX_OK;
    }

    size_t new_length;

    if (cx_add_overflow(bytes->length, length, &new_length)) {
        return CX_OVERFLOW;
    }

    const void* source;
    void* staged;
    auto status = bytes_stage(bytes, data, length, &source, &staged);

    if (status != CX_OK) {
        return status;
    }

    status = bytes_reserve_impl(bytes, new_length);

    if (status == CX_OK) {
        memmove(bytes->data + index + length, bytes->data + index, bytes->length - index);
        memcpy(bytes->data + index, source, length);
        bytes->length = new_length;
    }

    bytes_unstage(bytes, staged, length);
    return status;
}

cx_status cx_bytes_erase(cx_bytes* bytes, size_t index, size_t count) {
    if (bytes == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (index > bytes->length) {
        return CX_OUT_OF_RANGE;
    }

    auto remaining = bytes->length - index;

    if (count > remaining) {
        count = remaining;
    }

    if (count == 0) {
        return CX_OK;
    }

    memmove(bytes->data + index, bytes->data + index + count, bytes->length - index - count);
    bytes->length -= count;
    return CX_OK;
}

cx_status cx_bytes_replace(cx_bytes* bytes, size_t index, size_t count, const void* data,
                           size_t length) {
    if (bytes == nullptr || (data == nullptr && length != 0)) {
        return CX_INVALID_ARGUMENT;
    }

    if (index > bytes->length) {
        return CX_OUT_OF_RANGE;
    }

    auto remaining = bytes->length - index;

    if (count > remaining) {
        count = remaining;
    }

    size_t new_length;

    if (cx_add_overflow(bytes->length - count, length, &new_length)) {
        return CX_OVERFLOW;
    }

    if (length == 0 && count == 0) {
        return CX_OK;
    }

    const void* source;
    void* staged;
    auto status = bytes_stage(bytes, data, length, &source, &staged);

    if (status != CX_OK) {
        return status;
    }

    status = bytes_reserve_impl(bytes, new_length);

    if (status == CX_OK) {
        if (length != count) {
            memmove(bytes->data + index + length, bytes->data + index + count,
                    bytes->length - index - count);
        }

        if (length != 0) {
            memcpy(bytes->data + index, source, length);
        }

        bytes->length = new_length;
    }

    bytes_unstage(bytes, staged, length);
    return status;
}

void cx_bytes_swap(cx_bytes* left, cx_bytes* right) {
    if (left == nullptr || right == nullptr || left == right) {
        return;
    }

    auto temporary = *left;
    *left = *right;
    *right = temporary;
}

size_t cx_bytes_length(const cx_bytes* bytes) { return bytes != nullptr ? bytes->length : 0; }

size_t cx_bytes_capacity(const cx_bytes* bytes) { return bytes != nullptr ? bytes->capacity : 0; }

const uint8_t* cx_bytes_data(const cx_bytes* bytes) {
    return bytes != nullptr ? bytes->data : nullptr;
}

bool cx_bytes_empty(const cx_bytes* bytes) { return bytes == nullptr || bytes->length == 0; }

bool cx_bytes_is_null(const cx_bytes* bytes) { return bytes == nullptr || bytes->data == nullptr; }

bool cx_bytes_equals(const cx_bytes* left, const cx_bytes* right) {
    if (left == right) {
        return true;
    }

    if (left == nullptr || right == nullptr) {
        return false;
    }

    if (left->length != right->length) {
        return false;
    }

    if (left->length == 0) {
        return true;
    }

    return memcmp(left->data, right->data, left->length) == 0;
}

cx_slice cx_bytes_as_slice(const cx_bytes* bytes) {
    if (bytes == nullptr) {
        return (cx_slice){};
    }

    return cx_slice_from_parts(bytes->data, bytes->length);
}
