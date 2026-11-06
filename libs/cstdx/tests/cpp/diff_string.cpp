#include "diff_common.h"

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>

using namespace std::string_literals;

using cstdx_diff::CxString;
using cstdx_diff::contents_of;
using cstdx_diff::make_string;

TEST(StringDiff, FromMatchesStdString) {
    const std::string value = "hello\0world"s;

    auto string = make_string(value);

    ASSERT_NE(string.get(), nullptr);
    EXPECT_EQ(cx_string_length(string.get()), value.size());
    EXPECT_EQ(cx_string_capacity(string.get()) >= value.size(), true);
    EXPECT_EQ(cx_string_empty(string.get()), value.empty());
    EXPECT_EQ(cx_string_is_null(string.get()), value.empty());
    EXPECT_EQ(contents_of(string.get()), value);
    EXPECT_EQ(std::memcmp(cx_string_cstr(string.get()), value.data(),
                          value.size()),
              0);
    EXPECT_EQ(cx_string_cstr(string.get())[value.size()], '\0');
}

TEST(StringDiff, CopyAndMoveMatchValueSemantics) {
    auto original = make_string("alpha beta gamma");

    CxString copy;
    ASSERT_EQ(cx_string_copy(copy.out(), original.get()), CX_OK);
    ASSERT_NE(copy.get(), nullptr);
    EXPECT_EQ(contents_of(copy.get()), contents_of(original.get()));
    EXPECT_NE(cx_string_cstr(copy.get()), cx_string_cstr(original.get()));

    CxString moved;
    ASSERT_EQ(cx_string_init(moved.out(), nullptr), CX_OK);
    cx_string_move(moved.get(), copy.get());
    EXPECT_EQ(contents_of(moved.get()), contents_of(original.get()));
    EXPECT_EQ(cx_string_length(copy.get()), 0u);
    EXPECT_TRUE(cx_string_empty(copy.get()));
}

TEST(StringDiff, InsertEraseReplaceMatchesStdString) {
    std::string model = "hello world";
    auto string = make_string(model);

    ASSERT_EQ(cx_string_insert(string.get(), 5, cstdx_diff::view_of(" there")),
              CX_OK);
    model.insert(5, " there");
    EXPECT_EQ(contents_of(string.get()), model);

    ASSERT_EQ(cx_string_erase(string.get(), 0, 6), CX_OK);
    model.erase(0, 6);
    EXPECT_EQ(contents_of(string.get()), model);

    ASSERT_EQ(cx_string_replace(string.get(), 1, 2,
                                cstdx_diff::view_of("ELLO")),
              CX_OK);
    model.replace(1, 2, "ELLO");
    EXPECT_EQ(contents_of(string.get()), model);

    ASSERT_EQ(cx_string_push_back(string.get(), '!'), CX_OK);
    model.push_back('!');
    EXPECT_EQ(contents_of(string.get()), model);

    char popped = 0;
    ASSERT_EQ(cx_string_pop_back(string.get(), &popped), CX_OK);
    model.pop_back();
    EXPECT_EQ(popped, '!');
    EXPECT_EQ(contents_of(string.get()), model);
}

TEST(StringDiff, AliasingOperationsMatchStdString) {
    std::string model = "abc";
    auto string = make_string(model);

    ASSERT_EQ(cx_string_append(string.get(), string.get()), CX_OK);
    model += model;
    EXPECT_EQ(contents_of(string.get()), model);

    ASSERT_EQ(cx_string_insert(string.get(), 3, cx_string_view_of(string.get())),
              CX_OK);
    model.insert(3, model);
    EXPECT_EQ(contents_of(string.get()), model);

    ASSERT_EQ(cx_string_assign_view(string.get(), cstdx_diff::view_of(model)),
              CX_OK);
    EXPECT_EQ(contents_of(string.get()), model);
}

TEST(StringDiff, SearchAndCompareMatchStdString) {
    const std::string haystack = "the quick brown fox";
    auto string = make_string(haystack);

    for (const std::string needle : {"the", "quick", "fox", "missing", ""}) {
        size_t index = 0;
        const auto status = cx_string_find(string.get(), cstdx_diff::view_of(needle),
                                           &index);

        if (haystack.find(needle) == std::string::npos) {
            EXPECT_EQ(status, CX_NOT_FOUND);
        } else {
            EXPECT_EQ(status, CX_OK);
            EXPECT_EQ(index, haystack.find(needle));
        }
    }

    EXPECT_EQ(cx_string_contains(string.get(), cstdx_diff::view_of("brown")),
              haystack.find("brown") != std::string::npos);
    EXPECT_EQ(cx_string_starts_with(string.get(), cstdx_diff::view_of("the")),
              haystack.starts_with("the"));
    EXPECT_EQ(cx_string_ends_with(string.get(), cstdx_diff::view_of("fox")),
              haystack.ends_with("fox"));
}

RC_GTEST_PROP(StringDiff, FromViewRoundTripsArbitraryBytes,
              (const std::string &value)) {
    auto string = make_string(value);

    RC_ASSERT(cx_string_length(string.get()) == value.size());
    RC_ASSERT(contents_of(string.get()) == value);
    RC_ASSERT(std::memcmp(cx_string_cstr(string.get()), value.data(),
                          value.size()) == 0);
    RC_ASSERT(cx_string_cstr(string.get())[value.size()] == '\0');

    CxString copy;
    RC_ASSERT(cx_string_copy(copy.out(), string.get()) == CX_OK);
    RC_ASSERT(contents_of(copy.get()) == value);
    RC_ASSERT(cx_string_compare(string.get(), copy.get()) == CX_STRING_EQUAL);
}

RC_GTEST_PROP(StringDiff, AppendConcatenatesLikeStdString,
              (const std::string &left, const std::string &right)) {
    auto string = make_string(left);

    RC_ASSERT(cx_string_append_view(string.get(), cstdx_diff::view_of(right)) ==
              CX_OK);
    RC_ASSERT(contents_of(string.get()) == left + right);

    RC_ASSERT(cx_string_assign_view(string.get(), cstdx_diff::view_of(left)) ==
              CX_OK);
    RC_ASSERT(cx_string_append(string.get(), string.get()) == CX_OK);
    RC_ASSERT(contents_of(string.get()) == left + left);
}

RC_GTEST_PROP(StringDiff, InsertAndEraseMatchStdString,
              (const std::string &base, const std::string &insert,
               const uint32_t &position_seed)) {
    auto model = base;
    const size_t position =
        model.empty() ? 0 : position_seed % (model.size() + 1);
    auto string = make_string(base);

    RC_ASSERT(cx_string_insert(string.get(), position,
                               cstdx_diff::view_of(insert)) == CX_OK);
    model.insert(position, insert);
    RC_ASSERT(contents_of(string.get()) == model);

    if (!model.empty()) {
        const size_t start = position_seed % model.size();
        const size_t count = position_seed % (model.size() - start + 1);

        RC_ASSERT(cx_string_erase(string.get(), start, count) == CX_OK);
        model.erase(start, count);
        RC_ASSERT(contents_of(string.get()) == model);
    }
}

RC_GTEST_PROP(StringDiff, ResizeZeroFillsLikeStdString,
              (const std::string &base, const uint32_t &size_seed)) {
    auto model = base;
    const size_t target = size_seed % 64;
    auto string = make_string(base);

    RC_ASSERT(cx_string_resize(string.get(), target) == CX_OK);
    model.resize(target, '\0');
    RC_ASSERT(contents_of(string.get()) == model);
    RC_ASSERT(cx_string_length(string.get()) == target);
}
