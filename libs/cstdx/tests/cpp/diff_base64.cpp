#include "diff_common.h"

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using cstdx_diff::CxBytes;
using cstdx_diff::CxString;
using cstdx_diff::contents_of;

namespace {

CxString encode(const std::vector<uint8_t> &input) {
    CxString result;
    auto status = cx_base64_encode(
        cx_slice_from_parts(input.data(), input.size()), result.out(), nullptr);

    if (status != CX_OK) {
        cstdx_diff::fail("cx_base64_encode failed");
    }

    return result;
}

std::string encode_text(const std::vector<uint8_t> &input) {
    auto encoded = encode(input);
    return contents_of(encoded.get());
}

} // namespace

TEST(Base64Diff, KnownVectorsMatchReference) {
    const std::vector<std::vector<uint8_t>> cases = {
        {},
        {'f'},
        {'f', 'o'},
        {'f', 'o', 'o'},
        {'f', 'o', 'o', 'b'},
        {'f', 'o', 'o', 'b', 'a'},
        {'f', 'o', 'o', 'b', 'a', 'r'},
        {0x00, 0xFF, 0x10},
    };

    for (const auto &input : cases) {
        EXPECT_EQ(encode_text(input), cstdx_diff::base64_encode(input));
    }

    EXPECT_EQ(encode_text({}), "");
    EXPECT_EQ(encode_text({'f'}), "Zg==");
    EXPECT_EQ(encode_text({'f', 'o'}), "Zm8=");
    EXPECT_EQ(encode_text({'f', 'o', 'o'}), "Zm9v");
    EXPECT_EQ(encode_text({0x00, 0xFF, 0x10}), "AP8Q");
}

TEST(Base64Diff, DecodeMatchesReference) {
    const std::vector<std::string> cases = {
        "",
        "Zg==",
        "Zg",
        "Zm8=",
        "Zm8",
        "Zm9v",
        " Zm9v\nYmFy\t",
        "A",
        "Zg=",
        "Zg===",
        "Z=g=",
        "Zm9v!",
        "Zg==Zg==",
        "Zh==",
        "Zm9=",
        "====",
    };

    for (const auto &text : cases) {
        auto expected = cstdx_diff::base64_decode(text);
        CxBytes decoded;
        auto status = cx_base64_decode(
            cx_string_view{text.data(), text.size()}, decoded.out(), nullptr);

        if (!expected) {
            EXPECT_EQ(status, CX_INVALID_ARGUMENT) << text;
            continue;
        }

        ASSERT_EQ(status, CX_OK) << text;
        EXPECT_EQ(contents_of(decoded.get()), *expected) << text;
    }
}

RC_GTEST_PROP(Base64Diff, EncodeMatchesReferenceForArbitraryBytes,
              (const std::vector<uint8_t> &input)) {
    auto encoded = encode(input);

    RC_ASSERT(contents_of(encoded.get()) == cstdx_diff::base64_encode(input));

    size_t expected_length = 0;
    RC_ASSERT(cx_base64_encoded_length(input.size(), &expected_length) ==
              CX_OK);
    RC_ASSERT(cx_string_length(encoded.get()) == expected_length);
}

RC_GTEST_PROP(Base64Diff, EncodeDecodeRoundTripsArbitraryBytes,
              (const std::vector<uint8_t> &input)) {
    auto encoded = encode(input);

    CxBytes decoded;
    RC_ASSERT(cx_base64_decode(cx_string_view_of(encoded.get()), decoded.out(),
                               nullptr) == CX_OK);
    RC_ASSERT(contents_of(decoded.get()) == input);
}

RC_GTEST_PROP(Base64Diff, DecodeMatchesReferenceForArbitraryText,
              (const std::string &text)) {
    auto expected = cstdx_diff::base64_decode(text);
    CxBytes decoded;
    auto status = cx_base64_decode(
        cx_string_view{text.data(), text.size()}, decoded.out(), nullptr);

    if (!expected) {
        RC_ASSERT(status == CX_INVALID_ARGUMENT);
        return;
    }

    RC_ASSERT(status == CX_OK);
    RC_ASSERT(contents_of(decoded.get()) == *expected);
}

RC_GTEST_PROP(Base64Diff, WhitespaceIsIgnored,
              (const std::vector<uint8_t> &input, const uint32_t &seed)) {
    auto encoded = encode_text(input);
    std::string spaced;
    spaced.reserve(encoded.size() * 2);

    for (size_t i = 0; i < encoded.size(); ++i) {
        if ((seed >> (i % 16)) & 1u) {
            spaced.push_back(" \t\n\r"[i % 4]);
        }

        spaced.push_back(encoded[i]);
    }

    CxBytes decoded;
    RC_ASSERT(cx_base64_decode(
                  cx_string_view{spaced.data(), spaced.size()}, decoded.out(),
                  nullptr) == CX_OK);
    RC_ASSERT(contents_of(decoded.get()) == input);
}
