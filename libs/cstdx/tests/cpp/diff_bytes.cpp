#include "diff_common.h"

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

using cstdx_diff::CxBytes;
using cstdx_diff::contents_of;
using cstdx_diff::make_bytes;

TEST(BytesDiff, AssignMatchesStdVector) {
    const std::vector<uint8_t> value = {0x00, 0xFF, 0x7F, 0x80, 0x01};
    auto bytes = make_bytes(value);

    ASSERT_NE(bytes.get(), nullptr);
    EXPECT_EQ(cx_bytes_length(bytes.get()), value.size());
    EXPECT_EQ(cx_bytes_empty(bytes.get()), value.empty());
    EXPECT_EQ(cx_bytes_is_null(bytes.get()), value.empty());
    EXPECT_EQ(contents_of(bytes.get()), value);

    auto slice = cx_bytes_as_slice(bytes.get());
    EXPECT_EQ(slice.length, value.size());
    EXPECT_EQ(std::memcmp(slice.data, value.data(), value.size()), 0);
}

TEST(BytesDiff, CopyAndMoveMatchValueSemantics) {
    const std::vector<uint8_t> value = {1, 2, 3, 4};
    auto original = make_bytes(value);

    CxBytes copy;
    ASSERT_EQ(cx_bytes_copy(copy.out(), original.get()), CX_OK);
    ASSERT_NE(copy.get(), nullptr);
    EXPECT_EQ(contents_of(copy.get()), value);
    EXPECT_NE(cx_bytes_data(copy.get()), cx_bytes_data(original.get()));

    CxBytes moved;
    ASSERT_EQ(cx_bytes_init(moved.out(), nullptr), CX_OK);
    cx_bytes_move(moved.get(), copy.get());
    EXPECT_EQ(contents_of(moved.get()), value);
    EXPECT_EQ(cx_bytes_length(copy.get()), 0u);
    EXPECT_TRUE(cx_bytes_empty(copy.get()));
}

TEST(BytesDiff, InsertEraseReplaceMatchStdVector) {
    std::vector<uint8_t> model = {'a', 'b', 'c', 'd'};
    auto bytes = make_bytes(model);

    const uint8_t tail[] = {'x', 'y'};
    ASSERT_EQ(cx_bytes_insert(bytes.get(), 2, tail, sizeof(tail)), CX_OK);
    model.insert(model.begin() + 2, std::begin(tail), std::end(tail));
    EXPECT_EQ(contents_of(bytes.get()), model);

    ASSERT_EQ(cx_bytes_erase(bytes.get(), 1, 2), CX_OK);
    model.erase(model.begin() + 1, model.begin() + 3);
    EXPECT_EQ(contents_of(bytes.get()), model);

    const uint8_t replacement[] = {'Q'};
    ASSERT_EQ(cx_bytes_replace(bytes.get(), 1, 2, replacement,
                               sizeof(replacement)),
              CX_OK);
    model.erase(model.begin() + 1, model.begin() + 3);
    model.insert(model.begin() + 1, std::begin(replacement),
                 std::end(replacement));
    EXPECT_EQ(contents_of(bytes.get()), model);
}

TEST(BytesDiff, AliasingOperationsMatchStdVector) {
    const std::vector<uint8_t> value = {'a', 'b', 'c'};
    auto bytes = make_bytes(value);

    ASSERT_EQ(cx_bytes_append_slice(bytes.get(), cx_bytes_as_slice(bytes.get())),
              CX_OK);
    auto model = value;
    model.insert(model.end(), value.begin(), value.end());
    EXPECT_EQ(contents_of(bytes.get()), model);

    ASSERT_EQ(cx_bytes_insert(bytes.get(), 3, cx_bytes_data(bytes.get()), 3),
              CX_OK);
    model.insert(model.begin() + 3, value.begin(), value.end());
    EXPECT_EQ(contents_of(bytes.get()), model);
}

RC_GTEST_PROP(BytesDiff, AssignRoundTripsArbitraryBytes,
              (const std::vector<uint8_t> &value)) {
    auto bytes = make_bytes(value);

    RC_ASSERT(cx_bytes_length(bytes.get()) == value.size());
    RC_ASSERT(contents_of(bytes.get()) == value);
    RC_ASSERT(cx_bytes_empty(bytes.get()) == value.empty());
    RC_ASSERT(cx_bytes_equals(bytes.get(), bytes.get()));

    CxBytes copy;
    RC_ASSERT(cx_bytes_copy(copy.out(), bytes.get()) == CX_OK);
    RC_ASSERT(contents_of(copy.get()) == value);
}

RC_GTEST_PROP(BytesDiff, AppendConcatenatesLikeStdVector,
              (const std::vector<uint8_t> &left,
               const std::vector<uint8_t> &right)) {
    auto bytes = make_bytes(left);

    RC_ASSERT(cx_bytes_append(bytes.get(), right.data(), right.size()) ==
              CX_OK);
    auto model = left;
    model.insert(model.end(), right.begin(), right.end());
    RC_ASSERT(contents_of(bytes.get()) == model);
}

RC_GTEST_PROP(BytesDiff, InsertAndEraseMatchStdVector,
              (const std::vector<uint8_t> &base,
               const std::vector<uint8_t> &insert,
               const uint32_t &position_seed)) {
    auto model = base;
    const size_t position =
        model.empty() ? 0 : position_seed % (model.size() + 1);
    auto bytes = make_bytes(base);

    RC_ASSERT(cx_bytes_insert(bytes.get(), position, insert.data(),
                              insert.size()) == CX_OK);
    model.insert(model.begin() + static_cast<ptrdiff_t>(position),
                 insert.begin(), insert.end());
    RC_ASSERT(contents_of(bytes.get()) == model);

    if (!model.empty()) {
        const size_t start = position_seed % model.size();
        const size_t count = position_seed % (model.size() - start + 1);

        RC_ASSERT(cx_bytes_erase(bytes.get(), start, count) == CX_OK);
        model.erase(model.begin() + static_cast<ptrdiff_t>(start),
                    model.begin() + static_cast<ptrdiff_t>(start + count));
        RC_ASSERT(contents_of(bytes.get()) == model);
    }
}

RC_GTEST_PROP(BytesDiff, ResizeZeroFillsLikeStdVector,
              (const std::vector<uint8_t> &base,
               const uint32_t &size_seed)) {
    auto model = base;
    const size_t target = size_seed % 64;
    auto bytes = make_bytes(base);

    RC_ASSERT(cx_bytes_resize(bytes.get(), target) == CX_OK);
    model.resize(target, 0);
    RC_ASSERT(contents_of(bytes.get()) == model);
    RC_ASSERT(cx_bytes_length(bytes.get()) == target);
}
