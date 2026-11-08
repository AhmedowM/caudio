#include <cstdx/checked.h>
#include <stdint.h>
#include <string.h>

#include "string_internal.h"

static const cx_allocator* string_allocator(const cx_string* str) {
    return str->allocator != nullptr ? str->allocator : cx_allocator_default();
}

static void string_terminate(cx_string* str) {
    if (str->data != nullptr) {
        str->data[str->length] = '\0';
    }
}

static bool string_contains_range(const cx_string* str, const char* data, size_t length) {
    if (str->data == nullptr || data == nullptr) {
        return false;
    }

    size_t size;

    if (cx_add_overflow(str->capacity, 1, &size)) {
        return false;
    }

    auto base = (uintptr_t)str->data;
    auto end = base + (uintptr_t)size;
    auto start = (uintptr_t)data;

    if (start < base || start > end) {
        return false;
    }

    return (size_t)(end - start) >= length;
}

/**
 * Copy a view out of the destination buffer when it aliases it.
 *
 * Growing the destination may move its buffer, so an aliased source must be
 * detached first.
 */
static cx_status string_stage(cx_string* str, cx_string_view view, cx_string_view* source,
                              char** staged) {
    *staged = nullptr;
    *source = view;

    if (view.length == 0 || !string_contains_range(str, view.data, view.length)) {
        return CX_OK;
    }

    auto copy = cx_allocator_alloc(string_allocator(str), view.length, 1);

    if (copy == nullptr) {
        return CX_OUT_OF_MEMORY;
    }

    memcpy(copy, view.data, view.length);
    source->data = copy;
    *staged = copy;
    return CX_OK;
}

static void string_unstage(cx_string* str, char* staged, size_t size) {
    if (staged != nullptr) {
        cx_allocator_free(string_allocator(str), staged, size, 1);
    }
}

static cx_status string_reserve_impl(cx_string* str, size_t min_capacity) {
    if (str == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (min_capacity <= str->capacity) {
        return CX_OK;
    }

    auto capacity = str->capacity;

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

    size_t bytes;

    if (cx_add_overflow(capacity, 1, &bytes)) {
        return CX_OVERFLOW;
    }

    auto allocator = string_allocator(str);
    void* data;

    if (str->data == nullptr) {
        data = cx_allocator_alloc(allocator, bytes, 1);
    } else {
        data = cx_allocator_realloc(allocator, str->data, str->capacity + 1, bytes, 1);
    }

    if (data == nullptr) {
        return CX_OUT_OF_MEMORY;
    }

    str->data = (char*)data;
    str->capacity = capacity;
    return CX_OK;
}

cx_status cx_impl_string_init(cx_string* str, const cx_allocator* allocator) {
    if (str == nullptr || !cx_allocator_is_valid(allocator)) {
        return CX_INVALID_ARGUMENT;
    }

    *str = (cx_string){
        .allocator = allocator != nullptr ? allocator : cx_allocator_default(),
    };
    return CX_OK;
}

void cx_impl_string_destroy(cx_string* str) {
    if (str == nullptr) {
        return;
    }

    auto allocator = str->allocator != nullptr ? str->allocator : cx_allocator_default();

    if (str->data != nullptr) {
        cx_allocator_free(allocator, str->data, str->capacity + 1, 1);
    }

    *str = (cx_string){
        .allocator = allocator,
    };
}

cx_status cx_string_init(cx_string** out, const cx_allocator* allocator) {
    if (out == nullptr || !cx_allocator_is_valid(allocator)) {
        return CX_INVALID_ARGUMENT;
    }

    *out = nullptr;

    auto resolved = allocator != nullptr ? allocator : cx_allocator_default();
    auto str = (cx_string*)cx_allocator_alloc(resolved, sizeof(cx_string), alignof(cx_string));

    if (str == nullptr) {
        return CX_OUT_OF_MEMORY;
    }

    *str = (cx_string){
        .allocator = resolved,
    };
    *out = str;
    return CX_OK;
}

cx_status cx_string_from(cx_string** out, const char* cstr, const cx_allocator* allocator) {
    if (cstr == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    return cx_string_from_view(out, cx_string_view_from_cstr(cstr), allocator);
}

cx_status cx_string_from_view(cx_string** out, cx_string_view view, const cx_allocator* allocator) {
    if (out == nullptr || (view.data == nullptr && view.length != 0)) {
        return CX_INVALID_ARGUMENT;
    }

    *out = nullptr;

    cx_string* str = nullptr;
    auto status = cx_string_init(&str, allocator);

    if (status != CX_OK) {
        return status;
    }

    status = cx_string_assign_view(str, view);

    if (status != CX_OK) {
        cx_string_destroy(&str);
        return status;
    }

    *out = str;
    return CX_OK;
}

cx_status cx_string_copy(cx_string** out, const cx_string* src) {
    if (out == nullptr || src == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    *out = nullptr;
    return cx_string_from_view(out, cx_string_view_of(src), src->allocator);
}

void cx_string_move(cx_string* dst, cx_string* src) {
    if (dst == nullptr || src == nullptr || dst == src) {
        return;
    }

    auto src_allocator = src->allocator != nullptr ? src->allocator : cx_allocator_default();

    if (dst->data != nullptr) {
        auto dst_allocator = dst->allocator != nullptr ? dst->allocator : cx_allocator_default();

        cx_allocator_free(dst_allocator, dst->data, dst->capacity + 1, 1);
    }

    *dst = *src;
    *src = (cx_string){
        .allocator = src_allocator,
    };
}

void cx_string_destroy(cx_string** out) {
    if (out == nullptr || *out == nullptr) {
        return;
    }

    auto str = *out;
    *out = nullptr;

    auto allocator = str->allocator != nullptr ? str->allocator : cx_allocator_default();

    cx_impl_string_destroy(str);
    cx_allocator_free(allocator, str, sizeof(cx_string), alignof(cx_string));
}

const char* cx_string_at(const cx_string* str, size_t index) {
    if (str == nullptr || str->data == nullptr || index >= str->length) {
        return nullptr;
    }

    return str->data + index;
}

char* cx_string_at_mut(cx_string* str, size_t index) {
    if (str == nullptr || str->data == nullptr || index >= str->length) {
        return nullptr;
    }

    return str->data + index;
}

cx_status cx_string_get(const cx_string* str, size_t index, char* value) {
    if (str == nullptr || value == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (index >= str->length) {
        return CX_OUT_OF_RANGE;
    }

    *value = str->data[index];
    return CX_OK;
}

cx_status cx_string_set(cx_string* str, size_t index, char value) {
    if (str == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (index >= str->length) {
        return CX_OUT_OF_RANGE;
    }

    str->data[index] = value;
    return CX_OK;
}

cx_status cx_string_reserve(cx_string* str, size_t min_capacity) {
    return string_reserve_impl(str, min_capacity);
}

cx_status cx_string_resize(cx_string* str, size_t new_length) {
    if (str == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    auto status = string_reserve_impl(str, new_length);

    if (status != CX_OK) {
        return status;
    }

    if (new_length > str->length) {
        memset(str->data + str->length, 0, new_length - str->length);
    }

    str->length = new_length;
    string_terminate(str);
    return CX_OK;
}

cx_status cx_string_assign(cx_string* str, const char* cstr) {
    if (cstr == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    return cx_string_assign_view(str, cx_string_view_from_cstr(cstr));
}

cx_status cx_string_assign_view(cx_string* str, cx_string_view view) {
    if (str == nullptr || (view.data == nullptr && view.length != 0)) {
        return CX_INVALID_ARGUMENT;
    }

    cx_string_view source;
    char* staged;
    auto status = string_stage(str, view, &source, &staged);

    if (status != CX_OK) {
        return status;
    }

    status = string_reserve_impl(str, view.length);

    if (status == CX_OK) {
        if (view.length != 0) {
            memcpy(str->data, source.data, view.length);
        }

        str->length = view.length;
        string_terminate(str);
    }

    string_unstage(str, staged, view.length);
    return status;
}

void cx_string_clear(cx_string* str) {
    if (str == nullptr) {
        return;
    }

    str->length = 0;
    string_terminate(str);
}

cx_status cx_string_append(cx_string* str, const cx_string* other) {
    if (other == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    return cx_string_append_view(str, cx_string_view_of(other));
}

cx_status cx_string_append_cstr(cx_string* str, const char* cstr) {
    if (cstr == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    return cx_string_append_view(str, cx_string_view_from_cstr(cstr));
}

cx_status cx_string_append_view(cx_string* str, cx_string_view view) {
    if (str == nullptr || (view.data == nullptr && view.length != 0)) {
        return CX_INVALID_ARGUMENT;
    }

    size_t new_length;

    if (cx_add_overflow(str->length, view.length, &new_length)) {
        return CX_OVERFLOW;
    }

    if (view.length == 0) {
        return CX_OK;
    }

    cx_string_view source;
    char* staged;
    auto status = string_stage(str, view, &source, &staged);

    if (status != CX_OK) {
        return status;
    }

    status = string_reserve_impl(str, new_length);

    if (status == CX_OK) {
        memcpy(str->data + str->length, source.data, view.length);
        str->length = new_length;
        string_terminate(str);
    }

    string_unstage(str, staged, view.length);
    return status;
}

cx_status cx_string_push_back(cx_string* str, char value) {
    if (str == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    size_t new_length;

    if (cx_add_overflow(str->length, 1, &new_length)) {
        return CX_OVERFLOW;
    }

    auto status = string_reserve_impl(str, new_length);

    if (status != CX_OK) {
        return status;
    }

    str->data[str->length] = value;
    str->length = new_length;
    string_terminate(str);
    return CX_OK;
}

cx_status cx_string_pop_back(cx_string* str, char* value) {
    if (str == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (str->length == 0) {
        return CX_OUT_OF_RANGE;
    }

    str->length--;

    if (value != nullptr) {
        *value = str->data[str->length];
    }

    string_terminate(str);
    return CX_OK;
}

cx_status cx_string_insert(cx_string* str, size_t index, cx_string_view view) {
    if (str == nullptr || (view.data == nullptr && view.length != 0)) {
        return CX_INVALID_ARGUMENT;
    }

    if (index > str->length) {
        return CX_OUT_OF_RANGE;
    }

    size_t new_length;

    if (cx_add_overflow(str->length, view.length, &new_length)) {
        return CX_OVERFLOW;
    }

    if (view.length == 0) {
        return CX_OK;
    }

    cx_string_view source;
    char* staged;
    auto status = string_stage(str, view, &source, &staged);

    if (status != CX_OK) {
        return status;
    }

    status = string_reserve_impl(str, new_length);

    if (status == CX_OK) {
        memmove(str->data + index + view.length, str->data + index, str->length - index);
        memcpy(str->data + index, source.data, view.length);
        str->length = new_length;
        string_terminate(str);
    }

    string_unstage(str, staged, view.length);
    return status;
}

cx_status cx_string_erase(cx_string* str, size_t index, size_t count) {
    if (str == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (index > str->length) {
        return CX_OUT_OF_RANGE;
    }

    auto remaining = str->length - index;

    if (count > remaining) {
        count = remaining;
    }

    if (count == 0) {
        return CX_OK;
    }

    memmove(str->data + index, str->data + index + count, str->length - index - count);
    str->length -= count;
    string_terminate(str);
    return CX_OK;
}

cx_status cx_string_replace(cx_string* str, size_t index, size_t count, cx_string_view view) {
    if (str == nullptr || (view.data == nullptr && view.length != 0)) {
        return CX_INVALID_ARGUMENT;
    }

    if (index > str->length) {
        return CX_OUT_OF_RANGE;
    }

    auto remaining = str->length - index;

    if (count > remaining) {
        count = remaining;
    }

    size_t new_length;

    if (cx_add_overflow(str->length - count, view.length, &new_length)) {
        return CX_OVERFLOW;
    }

    if (view.length == 0 && count == 0) {
        return CX_OK;
    }

    cx_string_view source;
    char* staged;
    auto status = string_stage(str, view, &source, &staged);

    if (status != CX_OK) {
        return status;
    }

    status = string_reserve_impl(str, new_length);

    if (status == CX_OK) {
        if (view.length != count) {
            memmove(str->data + index + view.length, str->data + index + count,
                    str->length - index - count);
        }

        if (view.length != 0) {
            memcpy(str->data + index, source.data, view.length);
        }

        str->length = new_length;
        string_terminate(str);
    }

    string_unstage(str, staged, view.length);
    return status;
}

void cx_string_swap(cx_string* left, cx_string* right) {
    if (left == nullptr || right == nullptr || left == right) {
        return;
    }

    auto temporary = *left;
    *left = *right;
    *right = temporary;
}

cx_status cx_string_find(const cx_string* str, cx_string_view needle, size_t* index) {
    if (str == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    return cx_string_view_find(cx_string_view_of(str), needle, index);
}

cx_status cx_string_find_cstr(const cx_string* str, const char* needle, size_t* index) {
    if (needle == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    return cx_string_find(str, cx_string_view_from_cstr(needle), index);
}

bool cx_string_contains(const cx_string* str, cx_string_view needle) {
    if (str == nullptr) {
        return false;
    }

    size_t index;

    return cx_string_view_find(cx_string_view_of(str), needle, &index) == CX_OK;
}

bool cx_string_starts_with(const cx_string* str, cx_string_view prefix) {
    return str != nullptr && cx_string_view_starts_with(cx_string_view_of(str), prefix);
}

bool cx_string_ends_with(const cx_string* str, cx_string_view suffix) {
    return str != nullptr && cx_string_view_ends_with(cx_string_view_of(str), suffix);
}

size_t cx_string_length(const cx_string* str) { return str != nullptr ? str->length : 0; }

size_t cx_string_capacity(const cx_string* str) { return str != nullptr ? str->capacity : 0; }

const char* cx_string_cstr(const cx_string* str) {
    return str != nullptr && str->data != nullptr ? str->data : "";
}

bool cx_string_empty(const cx_string* str) { return str == nullptr || str->length == 0; }

bool cx_string_is_null(const cx_string* str) { return str == nullptr || str->data == nullptr; }

bool cx_string_equals(const cx_string* left, const cx_string* right) {
    if (left == right) {
        return true;
    }

    if (left == nullptr || right == nullptr) {
        return false;
    }

    return cx_string_view_eq(cx_string_view_of(left), cx_string_view_of(right));
}

bool cx_string_equals_cstr(const cx_string* str, const char* cstr) {
    if (str == nullptr || cstr == nullptr) {
        return false;
    }

    return cx_string_view_eq(cx_string_view_of(str), cx_string_view_from_cstr(cstr));
}

cx_string_compare_result cx_string_compare(const cx_string* left, const cx_string* right) {
    if (left == right) {
        return CX_STRING_EQUAL;
    }

    if (left == nullptr) {
        return CX_STRING_LESS;
    }

    if (right == nullptr) {
        return CX_STRING_GREATER;
    }

    return cx_string_view_compare(cx_string_view_of(left), cx_string_view_of(right));
}

cx_string_compare_result cx_string_compare_cstr(const cx_string* str, const char* cstr) {
    if (str == nullptr && cstr == nullptr) {
        return CX_STRING_EQUAL;
    }

    if (str == nullptr) {
        return CX_STRING_LESS;
    }

    if (cstr == nullptr) {
        return CX_STRING_GREATER;
    }

    return cx_string_view_compare(cx_string_view_of(str), cx_string_view_from_cstr(cstr));
}

cx_string_view cx_string_view_of(const cx_string* str) {
    if (str == nullptr) {
        return (cx_string_view){};
    }

    return cx_string_view_from_parts(str->data, str->length);
}
