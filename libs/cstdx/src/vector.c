#include <cstdx/checked.h>
#include <stdint.h>
#include <string.h>

#include "vector_internal.h"

static const cx_allocator* vector_allocator(const cx_vector* vector) {
    return vector->allocator != nullptr ? vector->allocator : cx_allocator_default();
}

static void vector_copy_element(const cx_vector* vector, void* dst, const void* src) {
    if (vector->ops != nullptr) {
        vector->ops->copy(dst, src);
    } else {
        memcpy(dst, src, vector->element_size);
    }
}

static void vector_move_element(const cx_vector* vector, void* dst, void* src) {
    if (vector->ops != nullptr && vector->ops->move != nullptr) {
        vector->ops->move(dst, src);
    } else {
        vector_copy_element(vector, dst, src);
    }
}

static void vector_destroy_element(const cx_vector* vector, void* element) {
    if (vector->ops != nullptr) {
        vector->ops->destroy(element);
    }
}

static bool vector_contains(const cx_vector* vector, const void* pointer) {
    if (vector->data == nullptr || pointer == nullptr || vector->capacity == 0) {
        return false;
    }

    size_t bytes;

    if (cx_mul_overflow(vector->capacity, vector->element_size, &bytes)) {
        return false;
    }

    auto base = (uintptr_t)vector->data;
    auto end = base + (uintptr_t)bytes;
    auto address = (uintptr_t)pointer;

    return address >= base && address < end;
}

/**
 * Copy an aliased element into detached storage before the vector grows or
 * shifts.
 */
static cx_status vector_stage(cx_vector* vector, const void* element, const void** source,
                              void** staged) {
    *staged = nullptr;
    *source = element;

    if (!vector_contains(vector, element)) {
        return CX_OK;
    }

    auto copy =
        cx_allocator_alloc(vector_allocator(vector), vector->element_size, vector->element_align);

    if (copy == nullptr) {
        return CX_OUT_OF_MEMORY;
    }

    vector_copy_element(vector, copy, element);
    *source = copy;
    *staged = copy;
    return CX_OK;
}

static void vector_unstage(cx_vector* vector, void* staged) {
    if (staged != nullptr) {
        vector_destroy_element(vector, staged);
        cx_allocator_free(vector_allocator(vector), staged, vector->element_size,
                          vector->element_align);
    }
}

static void vector_relocate(cx_vector* vector, void* destination, void* source, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        auto dst = (uint8_t*)destination + i * vector->element_size;
        auto src = (uint8_t*)source + i * vector->element_size;

        vector_move_element(vector, dst, src);
        vector_destroy_element(vector, src);
    }
}

static cx_status vector_grow(cx_vector* vector, size_t minimum) {
    if (minimum <= vector->capacity) {
        return CX_OK;
    }

    auto capacity = vector->capacity;

    if (capacity == 0) {
        capacity = 8;
    }

    while (capacity < minimum) {
        size_t grown;

        if (cx_mul_overflow(capacity, 2, &grown)) {
            capacity = minimum;
            break;
        }

        capacity = grown;
    }

    size_t bytes;

    if (cx_mul_overflow(capacity, vector->element_size, &bytes)) {
        return CX_OVERFLOW;
    }

    auto allocator = vector_allocator(vector);
    auto data = cx_allocator_alloc(allocator, bytes, vector->element_align);

    if (data == nullptr) {
        return CX_OUT_OF_MEMORY;
    }

    if (vector->data != nullptr && vector->length != 0) {
        vector_relocate(vector, data, vector->data, vector->length);
    }

    if (vector->data != nullptr) {
        size_t old_bytes = vector->capacity * vector->element_size;

        cx_allocator_free(allocator, vector->data, old_bytes, vector->element_align);
    }

    vector->data = data;
    vector->capacity = capacity;
    return CX_OK;
}

cx_status cx_impl_vector_init(cx_vector* vector, size_t element_size, size_t element_align,
                              const cx_vector_ops* ops, const cx_allocator* allocator) {
    if (vector == nullptr || element_size == 0 || !cx_is_power_of_two(element_align) ||
        !cx_allocator_is_valid(allocator)) {
        return CX_INVALID_ARGUMENT;
    }

    if (ops != nullptr && (ops->copy == nullptr || ops->destroy == nullptr)) {
        return CX_INVALID_ARGUMENT;
    }

    *vector = (cx_vector){
        .element_size = element_size,
        .element_align = element_align,
        .ops = ops,
        .allocator = allocator != nullptr ? allocator : cx_allocator_default(),
    };
    return CX_OK;
}

void cx_impl_vector_destroy(cx_vector* vector) {
    if (vector == nullptr) {
        return;
    }

    auto allocator = vector_allocator(vector);

    cx_vector_clear(vector);

    if (vector->data != nullptr) {
        size_t bytes = vector->capacity * vector->element_size;

        cx_allocator_free(allocator, vector->data, bytes, vector->element_align);
    }

    *vector = (cx_vector){
        .allocator = allocator,
    };
}

cx_status cx_vector_init(cx_vector** out, size_t element_size, size_t element_align,
                         const cx_vector_ops* ops, const cx_allocator* allocator) {
    if (out == nullptr || !cx_allocator_is_valid(allocator)) {
        return CX_INVALID_ARGUMENT;
    }

    *out = nullptr;

    auto resolved = allocator != nullptr ? allocator : cx_allocator_default();
    auto vector = (cx_vector*)cx_allocator_alloc(resolved, sizeof(cx_vector), alignof(cx_vector));

    if (vector == nullptr) {
        return CX_OUT_OF_MEMORY;
    }

    auto status = cx_impl_vector_init(vector, element_size, element_align, ops, resolved);

    if (status != CX_OK) {
        cx_allocator_free(resolved, vector, sizeof(cx_vector), alignof(cx_vector));
        return status;
    }

    *out = vector;
    return CX_OK;
}

void cx_vector_destroy(cx_vector** out) {
    if (out == nullptr || *out == nullptr) {
        return;
    }

    auto vector = *out;
    *out = nullptr;

    auto allocator = vector_allocator(vector);

    cx_impl_vector_destroy(vector);
    cx_allocator_free(allocator, vector, sizeof(cx_vector), alignof(cx_vector));
}

cx_status cx_vector_reserve(cx_vector* vector, size_t capacity) {
    if (vector == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    return vector_grow(vector, capacity);
}

cx_status cx_vector_shrink_to_fit(cx_vector* vector) {
    if (vector == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (vector->length == vector->capacity) {
        return CX_OK;
    }

    if (vector->length == 0) {
        if (vector->data != nullptr) {
            size_t bytes = vector->capacity * vector->element_size;

            cx_allocator_free(vector_allocator(vector), vector->data, bytes, vector->element_align);
        }

        vector->data = nullptr;
        vector->capacity = 0;
        return CX_OK;
    }

    size_t bytes;

    if (cx_mul_overflow(vector->length, vector->element_size, &bytes)) {
        return CX_OVERFLOW;
    }

    auto allocator = vector_allocator(vector);
    auto data = cx_allocator_alloc(allocator, bytes, vector->element_align);

    if (data == nullptr) {
        return CX_OUT_OF_MEMORY;
    }

    vector_relocate(vector, data, vector->data, vector->length);

    size_t old_bytes = vector->capacity * vector->element_size;

    cx_allocator_free(allocator, vector->data, old_bytes, vector->element_align);

    vector->data = data;
    vector->capacity = vector->length;
    return CX_OK;
}

cx_status cx_vector_push_copy(cx_vector* vector, const void* element, size_t element_size) {
    if (vector == nullptr || element == nullptr || element_size != vector->element_size) {
        return CX_INVALID_ARGUMENT;
    }

    size_t new_length;

    if (cx_add_overflow(vector->length, 1, &new_length)) {
        return CX_OVERFLOW;
    }

    const void* source;
    void* staged;
    auto status = vector_stage(vector, element, &source, &staged);

    if (status != CX_OK) {
        return status;
    }

    status = vector_grow(vector, new_length);

    if (status == CX_OK) {
        auto destination = (uint8_t*)vector->data + vector->length * vector->element_size;

        vector_copy_element(vector, destination, source);
        vector->length = new_length;
    }

    vector_unstage(vector, staged);
    return status;
}

cx_status cx_vector_insert_copy(cx_vector* vector, size_t index, const void* element,
                                size_t element_size) {
    if (vector == nullptr || element == nullptr || element_size != vector->element_size) {
        return CX_INVALID_ARGUMENT;
    }

    if (index > vector->length) {
        return CX_OUT_OF_RANGE;
    }

    size_t new_length;

    if (cx_add_overflow(vector->length, 1, &new_length)) {
        return CX_OVERFLOW;
    }

    const void* source;
    void* staged;
    auto status = vector_stage(vector, element, &source, &staged);

    if (status != CX_OK) {
        return status;
    }

    status = vector_grow(vector, new_length);

    if (status == CX_OK) {
        auto data = (uint8_t*)vector->data;
        auto size = vector->element_size;
        auto destination = data + index * size;

        if (vector->ops == nullptr) {
            memmove(data + (index + 1) * size, destination, (vector->length - index) * size);
            memcpy(destination, source, size);
        } else {
            for (size_t i = vector->length; i > index; --i) {
                auto dst = data + i * size;
                auto src = data + (i - 1) * size;

                vector_move_element(vector, dst, src);
                vector_destroy_element(vector, src);
            }

            vector_copy_element(vector, destination, source);
        }

        vector->length = new_length;
    }

    vector_unstage(vector, staged);
    return status;
}

cx_status cx_vector_pop(cx_vector* vector, void* out) {
    if (vector == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (vector->length == 0) {
        return CX_OUT_OF_RANGE;
    }

    if (out != nullptr && vector_contains(vector, out)) {
        return CX_INVALID_ARGUMENT;
    }

    auto last = (uint8_t*)vector->data + (vector->length - 1) * vector->element_size;

    if (out != nullptr) {
        vector_move_element(vector, out, last);
    }

    vector_destroy_element(vector, last);
    vector->length--;
    return CX_OK;
}

cx_status cx_vector_remove(cx_vector* vector, size_t index) {
    if (vector == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (index >= vector->length) {
        return CX_OUT_OF_RANGE;
    }

    auto data = (uint8_t*)vector->data;
    auto size = vector->element_size;

    if (vector->ops == nullptr) {
        memmove(data + index * size, data + (index + 1) * size,
                (vector->length - index - 1) * size);
    } else {
        vector_destroy_element(vector, data + index * size);

        for (size_t i = index; i + 1 < vector->length; ++i) {
            auto dst = data + i * size;
            auto src = data + (i + 1) * size;

            vector_move_element(vector, dst, src);
            vector_destroy_element(vector, src);
        }
    }

    vector->length--;
    return CX_OK;
}

cx_status cx_vector_swap_remove(cx_vector* vector, size_t index) {
    if (vector == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (index >= vector->length) {
        return CX_OUT_OF_RANGE;
    }

    auto data = (uint8_t*)vector->data;
    auto size = vector->element_size;
    auto last_index = vector->length - 1;

    if (index == last_index) {
        vector_destroy_element(vector, data + index * size);
    } else {
        auto destination = data + index * size;
        auto last = data + last_index * size;

        vector_destroy_element(vector, destination);
        vector_move_element(vector, destination, last);
        vector_destroy_element(vector, last);
    }

    vector->length--;
    return CX_OK;
}

void cx_vector_clear(cx_vector* vector) {
    if (vector == nullptr) {
        return;
    }

    if (vector->ops != nullptr && vector->ops->destroy != nullptr) {
        for (size_t i = 0; i < vector->length; ++i) {
            vector_destroy_element(vector, (uint8_t*)vector->data + i * vector->element_size);
        }
    }

    vector->length = 0;
}

const void* cx_vector_ptr(const cx_vector* vector, size_t index) {
    if (vector == nullptr || vector->data == nullptr || index >= vector->length) {
        return nullptr;
    }

    return (const uint8_t*)vector->data + index * vector->element_size;
}

void* cx_vector_ptr_mut(cx_vector* vector, size_t index) {
    if (vector == nullptr || vector->data == nullptr || index >= vector->length) {
        return nullptr;
    }

    return (uint8_t*)vector->data + index * vector->element_size;
}

const void* cx_vector_ptr_typed(const cx_vector* vector, size_t index, size_t element_size) {
    if (vector == nullptr || element_size != vector->element_size || index >= vector->length) {
        return nullptr;
    }

    return (const uint8_t*)vector->data + index * element_size;
}

void* cx_vector_ptr_mut_typed(cx_vector* vector, size_t index, size_t element_size) {
    if (vector == nullptr || element_size != vector->element_size || index >= vector->length) {
        return nullptr;
    }

    return (uint8_t*)vector->data + index * element_size;
}

size_t cx_vector_element_align(const cx_vector* vector) {
    return vector != nullptr ? vector->element_align : 0;
}

void* cx_vector_data_ptr(cx_vector* vector) { return vector != nullptr ? vector->data : nullptr; }

const void* cx_vector_data_ptr_const(const cx_vector* vector) {
    return vector != nullptr ? vector->data : nullptr;
}

size_t cx_vector_length(const cx_vector* vector) { return vector != nullptr ? vector->length : 0; }

size_t cx_vector_capacity(const cx_vector* vector) {
    return vector != nullptr ? vector->capacity : 0;
}

size_t cx_vector_element_size(const cx_vector* vector) {
    return vector != nullptr ? vector->element_size : 0;
}

bool cx_vector_empty(const cx_vector* vector) { return vector == nullptr || vector->length == 0; }

cx_slice cx_vector_as_slice(const cx_vector* vector) {
    if (vector == nullptr || vector->data == nullptr) {
        return (cx_slice){};
    }

    size_t bytes;

    if (cx_mul_overflow(vector->length, vector->element_size, &bytes)) {
        return (cx_slice){};
    }

    return cx_slice_from_parts(vector->data, bytes);
}
