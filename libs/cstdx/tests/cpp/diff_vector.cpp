#include "diff_common.h"

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <algorithm>
#include <cstring>
#include <vector>

using cstdx_diff::CxVector;
using cstdx_diff::contents_of;
using cstdx_diff::make_int_vector;

TEST(VectorDiff, PushMatchesStdVector) {
    const std::vector<int> value = {5, 1, 4, 2, 3};
    auto vector = make_int_vector(value);

    ASSERT_NE(vector.get(), nullptr);
    EXPECT_EQ(cx_vector_length(vector.get()), value.size());
    EXPECT_EQ(cx_vector_empty(vector.get()), value.empty());
    EXPECT_EQ(cx_vector_element_size(vector.get()), sizeof(int));
    EXPECT_EQ(cx_vector_element_align(vector.get()), alignof(int));
    EXPECT_EQ(contents_of(vector.get()), value);

    auto slice = cx_vector_as_slice(vector.get());
    EXPECT_EQ(slice.length, value.size() * sizeof(int));
    EXPECT_EQ(slice.data, cx_vector_data_ptr(vector.get()));
    EXPECT_EQ(std::memcmp(slice.data, value.data(), slice.length), 0);
}

TEST(VectorDiff, InsertRemoveAndPopMatchStdVector) {
    std::vector<int> model = {1, 2, 3};
    auto vector = make_int_vector(model);

    int value = 9;
    ASSERT_EQ(cx_vector_insert_copy(vector.get(), 1, &value, sizeof(value)),
              CX_OK);
    model.insert(model.begin() + 1, value);
    EXPECT_EQ(contents_of(vector.get()), model);

    ASSERT_EQ(cx_vector_remove(vector.get(), 2), CX_OK);
    model.erase(model.begin() + 2);
    EXPECT_EQ(contents_of(vector.get()), model);

    ASSERT_EQ(cx_vector_swap_remove(vector.get(), 0), CX_OK);
    model[0] = model.back();
    model.pop_back();
    EXPECT_EQ(contents_of(vector.get()), model);

    int popped = 0;
    ASSERT_EQ(cx_vector_pop(vector.get(), &popped), CX_OK);
    EXPECT_EQ(popped, model.back());
    model.pop_back();
    EXPECT_EQ(contents_of(vector.get()), model);
}

TEST(VectorDiff, ReserveAndShrinkPreserveContents) {
    const std::vector<int> value = {1, 2, 3, 4, 5};
    auto vector = make_int_vector(value);

    ASSERT_EQ(cx_vector_reserve(vector.get(), 256), CX_OK);
    EXPECT_GE(cx_vector_capacity(vector.get()), 256u);
    EXPECT_EQ(contents_of(vector.get()), value);

    ASSERT_EQ(cx_vector_shrink_to_fit(vector.get()), CX_OK);
    EXPECT_EQ(cx_vector_capacity(vector.get()), value.size());
    EXPECT_EQ(contents_of(vector.get()), value);

    cx_vector_clear(vector.get());
    EXPECT_TRUE(cx_vector_empty(vector.get()));
    EXPECT_GT(cx_vector_capacity(vector.get()), 0u);
}

RC_GTEST_PROP(VectorDiff, PushRoundTripsArbitraryInts,
              (const std::vector<int> &value)) {
    auto vector = make_int_vector(value);

    RC_ASSERT(cx_vector_length(vector.get()) == value.size());
    RC_ASSERT(contents_of(vector.get()) == value);
    RC_ASSERT(cx_vector_empty(vector.get()) == value.empty());
    RC_ASSERT(cx_vector_as_slice(vector.get()).length ==
              value.size() * sizeof(int));
}

RC_GTEST_PROP(VectorDiff, InsertAndRemoveMatchStdVector,
              (const std::vector<int> &base, const int &value,
               const uint32_t &position_seed)) {
    auto model = base;
    const size_t position =
        model.empty() ? 0 : position_seed % (model.size() + 1);
    auto vector = make_int_vector(base);

    RC_ASSERT(cx_vector_insert_copy(vector.get(), position, &value,
                                    sizeof(value)) == CX_OK);
    model.insert(model.begin() + static_cast<ptrdiff_t>(position), value);
    RC_ASSERT(contents_of(vector.get()) == model);

    if (!model.empty()) {
        const size_t index = position_seed % model.size();

        RC_ASSERT(cx_vector_remove(vector.get(), index) == CX_OK);
        model.erase(model.begin() + static_cast<ptrdiff_t>(index));
        RC_ASSERT(contents_of(vector.get()) == model);
    }
}

RC_GTEST_PROP(VectorDiff, SwapRemoveMatchesStdVector,
              (const std::vector<int> &base, const uint32_t &position_seed)) {
    auto model = base;
    auto vector = make_int_vector(base);

    if (!model.empty()) {
        const size_t index = position_seed % model.size();

        RC_ASSERT(cx_vector_swap_remove(vector.get(), index) == CX_OK);
        model[index] = model.back();
        model.pop_back();
        RC_ASSERT(contents_of(vector.get()) == model);
    }

    RC_ASSERT(cx_vector_length(vector.get()) == model.size());
}

RC_GTEST_PROP(VectorDiff, PopMatchesStdVector,
              (const std::vector<int> &base, const uint32_t &count_seed)) {
    auto model = base;
    auto vector = make_int_vector(base);
    const size_t pops = model.empty() ? 0 : count_seed % (model.size() + 1);

    for (size_t i = 0; i < pops; ++i) {
        int popped = 0;
        RC_ASSERT(cx_vector_pop(vector.get(), &popped) == CX_OK);
        RC_ASSERT(popped == model.back());
        model.pop_back();
    }

    RC_ASSERT(contents_of(vector.get()) == model);
    RC_ASSERT(cx_vector_pop(vector.get(), nullptr) ==
              (model.empty() ? CX_OUT_OF_RANGE : CX_OK));
}
