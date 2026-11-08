#include <cstdx/checked.h>
#include <cstdx/string_builder.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "string_builder_internal.h"
#include "string_internal.h"

cx_status cx_string_builder_init(cx_string_builder** out, const cx_allocator* allocator) {
    if (out == nullptr || !cx_allocator_is_valid(allocator)) {
        return CX_INVALID_ARGUMENT;
    }

    *out = nullptr;

    auto resolved = allocator != nullptr ? allocator : cx_allocator_default();
    auto builder = (cx_string_builder*)cx_allocator_alloc(resolved, sizeof(cx_string_builder),
                                                          alignof(cx_string_builder));

    if (builder == nullptr) {
        return CX_OUT_OF_MEMORY;
    }

    auto status = cx_impl_string_init(&builder->string, resolved);

    if (status != CX_OK) {
        cx_allocator_free(resolved, builder, sizeof(cx_string_builder), alignof(cx_string_builder));
        return status;
    }

    *out = builder;
    return CX_OK;
}

void cx_string_builder_destroy(cx_string_builder** out) {
    if (out == nullptr || *out == nullptr) {
        return;
    }

    auto builder = *out;
    *out = nullptr;

    auto allocator =
        builder->string.allocator != nullptr ? builder->string.allocator : cx_allocator_default();

    cx_impl_string_destroy(&builder->string);
    cx_allocator_free(allocator, builder, sizeof(cx_string_builder), alignof(cx_string_builder));
}

cx_status cx_string_builder_append(cx_string_builder* builder, cx_string_view view) {
    if (builder == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    return cx_string_append_view(&builder->string, view);
}

cx_status cx_string_builder_append_cstr(cx_string_builder* builder, const char* cstr) {
    if (builder == nullptr || cstr == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    return cx_string_append_view(&builder->string, cx_string_view_from_cstr(cstr));
}

cx_status cx_string_builder_append_char(cx_string_builder* builder, char value) {
    if (builder == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    return cx_string_push_back(&builder->string, value);
}

cx_status cx_string_builder_printf(cx_string_builder* builder, const char* format, ...) {
    if (builder == nullptr || format == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    va_list args;
    va_start(args, format);

    va_list measure;
    va_copy(measure, args);

    auto needed = vsnprintf(nullptr, 0, format, measure);

    va_end(measure);

    if (needed < 0) {
        va_end(args);
        return CX_INVALID_ARGUMENT;
    }

    if (needed == 0) {
        va_end(args);
        return CX_OK;
    }

    size_t new_length;

    if (cx_add_overflow(builder->string.length, (size_t)needed, &new_length)) {
        va_end(args);
        return CX_OVERFLOW;
    }

    auto status = cx_string_reserve(&builder->string, new_length);

    if (status != CX_OK) {
        va_end(args);
        return status;
    }

    auto available = builder->string.capacity - builder->string.length + 1;
    auto written =
        vsnprintf(builder->string.data + builder->string.length, available, format, args);

    va_end(args);

    if (written < 0 || (size_t)written != (size_t)needed) {
        return CX_INTERNAL;
    }

    builder->string.length = new_length;
    return CX_OK;
}

cx_string_view cx_string_builder_view(const cx_string_builder* builder) {
    if (builder == nullptr) {
        return (cx_string_view){};
    }

    return cx_string_view_of(&builder->string);
}

const char* cx_string_builder_cstr(const cx_string_builder* builder) {
    if (builder == nullptr) {
        return "";
    }

    return cx_string_cstr(&builder->string);
}

size_t cx_string_builder_length(const cx_string_builder* builder) {
    return builder != nullptr ? cx_string_length(&builder->string) : 0;
}

cx_status cx_string_builder_finish(cx_string_builder* builder, cx_string** out) {
    if (builder == nullptr || out == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    *out = nullptr;

    auto allocator =
        builder->string.allocator != nullptr ? builder->string.allocator : cx_allocator_default();
    auto str = (cx_string*)cx_allocator_alloc(allocator, sizeof(cx_string), alignof(cx_string));

    if (str == nullptr) {
        return CX_OUT_OF_MEMORY;
    }

    *str = builder->string;
    builder->string = (cx_string){
        .allocator = allocator,
    };
    *out = str;
    return CX_OK;
}
