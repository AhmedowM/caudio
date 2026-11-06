#include <cstdx/allocator_counted.h>
#include <cstdx/string_builder.h>

#include <string.h>

#include "cx_test.h"

static int test_append(void) {
    cx_string_builder *builder = nullptr;

    CX_CHECK(cx_string_builder_init(&builder, nullptr) == CX_OK);
    CX_CHECK(builder != nullptr);
    CX_CHECK_EQ(0, cx_string_builder_length(builder));
    CX_CHECK(strcmp(cx_string_builder_cstr(builder), "") == 0);

    CX_CHECK(cx_string_builder_append(builder,
                                      cx_string_view_from_cstr("hello")) ==
             CX_OK);
    CX_CHECK(cx_string_builder_append_char(builder, ' ') == CX_OK);
    CX_CHECK(cx_string_builder_append_cstr(builder, "world") == CX_OK);
    CX_CHECK(strcmp(cx_string_builder_cstr(builder), "hello world") == 0);
    CX_CHECK_EQ(11, cx_string_builder_length(builder));

    auto view = cx_string_builder_view(builder);

    CX_CHECK_EQ(11, view.length);
    CX_CHECK(memcmp(view.data, "hello world", 11) == 0);

    cx_string_builder_destroy(&builder);
    CX_CHECK(builder == nullptr);

    return cx_test_finish("string_builder.append");
}

static int test_printf(void) {
    cx_string_builder *builder = nullptr;

    CX_CHECK(cx_string_builder_init(&builder, nullptr) == CX_OK);
    CX_CHECK(cx_string_builder_printf(builder, "%s %d", "value", 42) == CX_OK);
    CX_CHECK(strcmp(cx_string_builder_cstr(builder), "value 42") == 0);

    CX_CHECK(cx_string_builder_printf(builder, " %s", "tail") == CX_OK);
    CX_CHECK(strcmp(cx_string_builder_cstr(builder), "value 42 tail") == 0);

    char large[300];

    memset(large, 'x', sizeof(large) - 1);
    large[sizeof(large) - 1] = '\0';

    CX_CHECK(cx_string_builder_printf(builder, "%s", large) == CX_OK);
    CX_CHECK_EQ(13 + 299, cx_string_builder_length(builder));
    CX_CHECK(cx_string_builder_cstr(builder)[8] == ' ');
    CX_CHECK(cx_string_builder_cstr(builder)[13] == 'x');

    CX_CHECK(cx_string_builder_printf(builder, "%s", "") == CX_OK);
    CX_CHECK_EQ(13 + 299, cx_string_builder_length(builder));

    cx_string_builder_destroy(&builder);

    return cx_test_finish("string_builder.printf");
}

static int test_finish(void) {
    cx_string_builder *builder = nullptr;

    CX_CHECK(cx_string_builder_init(&builder, nullptr) == CX_OK);
    CX_CHECK(cx_string_builder_append_cstr(builder, "finished") == CX_OK);

    cx_string *out = nullptr;

    CX_CHECK(cx_string_builder_finish(builder, &out) == CX_OK);
    CX_CHECK(strcmp(cx_string_cstr(out), "finished") == 0);
    CX_CHECK_EQ(0, cx_string_builder_length(builder));
    CX_CHECK(strcmp(cx_string_builder_cstr(builder), "") == 0);

    /* The builder can be reused after finish. */
    CX_CHECK(cx_string_builder_append_cstr(builder, "again") == CX_OK);
    CX_CHECK(strcmp(cx_string_builder_cstr(builder), "again") == 0);
    CX_CHECK(cx_string_builder_finish(builder, nullptr) == CX_INVALID_ARGUMENT);

    cx_string_destroy(&out);
    cx_string_builder_destroy(&builder);

    return cx_test_finish("string_builder.finish");
}

static int test_counted_allocator(void) {
    cx_allocator_counted counted;

    CX_CHECK(cx_allocator_counted_init(&counted, nullptr) == CX_OK);

    auto allocator = cx_allocator_counted_as_allocator(&counted);
    cx_string_builder *builder = nullptr;

    CX_CHECK(cx_string_builder_init(&builder, &allocator) == CX_OK);

    for (int i = 0; i < 100; ++i) {
        CX_CHECK(cx_string_builder_printf(builder, "[%d]", i) == CX_OK);
    }

    cx_string *out = nullptr;

    CX_CHECK(cx_string_builder_finish(builder, &out) == CX_OK);
    CX_CHECK_EQ(10 * 3 + 90 * 4, cx_string_length(out));

    cx_string_destroy(&out);
    cx_string_builder_destroy(&builder);
    CX_CHECK(!cx_allocator_counted_has_leaks(&counted));

    cx_allocator_counted_destroy(&counted);

    return cx_test_finish("string_builder.counted_allocator");
}

int main(void) {
    int failures = 0;

    failures += test_append();
    failures += test_printf();
    failures += test_finish();
    failures += test_counted_allocator();

    return failures == 0 ? 0 : 1;
}
