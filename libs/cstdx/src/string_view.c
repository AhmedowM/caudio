#include <cstdx/checked.h>
#include <cstdx/string_view.h>
#include <string.h>

cx_string_view cx_string_view_from_cstr(const char* cstr) {
    if (cstr == nullptr) {
        return (cx_string_view){};
    }

    return (cx_string_view){
        .data = cstr,
        .length = strlen(cstr),
    };
}

cx_string_view cx_string_view_from_parts(const char* data, size_t length) {
    if (data == nullptr) {
        length = 0;
    }

    return (cx_string_view){
        .data = data,
        .length = length,
    };
}

cx_status cx_string_view_sub(cx_string_view view, size_t start, size_t length,
                             cx_string_view* out) {
    if (out == nullptr) {
        return CX_INVALID_ARGUMENT;
    }

    if (start > view.length || length > view.length - start) {
        return CX_OUT_OF_RANGE;
    }

    *out = (cx_string_view){
        .data = view.data != nullptr ? view.data + start : nullptr,
        .length = length,
    };

    return CX_OK;
}

bool cx_string_view_eq(cx_string_view left, cx_string_view right) {
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

cx_string_compare_result cx_string_view_compare(cx_string_view left, cx_string_view right) {
    auto common = left.length < right.length ? left.length : right.length;

    if (common != 0) {
        auto order = memcmp(left.data, right.data, common);

        if (order < 0) {
            return CX_STRING_LESS;
        }

        if (order > 0) {
            return CX_STRING_GREATER;
        }
    }

    if (left.length < right.length) {
        return CX_STRING_LESS;
    }

    if (left.length > right.length) {
        return CX_STRING_GREATER;
    }

    return CX_STRING_EQUAL;
}

cx_status cx_string_view_find(cx_string_view haystack, cx_string_view needle, size_t* out) {
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

    auto limit = haystack.length - needle.length;

    for (size_t i = 0; i <= limit; ++i) {
        if (haystack.data[i] == needle.data[0] &&
            memcmp(haystack.data + i, needle.data, needle.length) == 0) {
            *out = i;
            return CX_OK;
        }
    }

    return CX_NOT_FOUND;
}

bool cx_string_view_starts_with(cx_string_view view, cx_string_view prefix) {
    if (prefix.length > view.length) {
        return false;
    }

    if (prefix.length == 0) {
        return true;
    }

    if (view.data == nullptr || prefix.data == nullptr) {
        return false;
    }

    return memcmp(view.data, prefix.data, prefix.length) == 0;
}

bool cx_string_view_ends_with(cx_string_view view, cx_string_view suffix) {
    if (suffix.length > view.length) {
        return false;
    }

    if (suffix.length == 0) {
        return true;
    }

    if (view.data == nullptr || suffix.data == nullptr) {
        return false;
    }

    return memcmp(view.data + view.length - suffix.length, suffix.data, suffix.length) == 0;
}

void cx_string_view_split_init(cx_string_view_split* split, cx_string_view input,
                               cx_string_view delimiter) {
    if (split == nullptr) {
        return;
    }

    *split = (cx_string_view_split){
        .input = input,
        .delimiter = delimiter,
        .offset = 0,
        .done = false,
    };
}

bool cx_string_view_split_next(cx_string_view_split* split, cx_string_view* out) {
    if (split == nullptr || out == nullptr || split->done) {
        return false;
    }

    if (split->delimiter.length == 0) {
        split->done = true;
        *out = cx_string_view_from_parts(
            split->input.data != nullptr ? split->input.data + split->offset : nullptr,
            split->input.length - split->offset);
        return true;
    }

    auto remaining = cx_string_view_from_parts(
        split->input.data != nullptr ? split->input.data + split->offset : nullptr,
        split->input.length - split->offset);
    size_t index = 0;

    if (cx_string_view_find(remaining, split->delimiter, &index) == CX_OK) {
        *out = cx_string_view_from_parts(remaining.data, index);
        split->offset += index + split->delimiter.length;
        return true;
    }

    split->done = true;
    *out = remaining;
    return true;
}
