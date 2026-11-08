#ifndef CSTDX_TESTS_CPP_DIFF_COMMON_H
#define CSTDX_TESTS_CPP_DIFF_COMMON_H

/**
 * @file diff_common.h
 * @brief Shared helpers for the C++ differential/oracle tests.
 *
 * The library itself is C-only. These helpers are test-only and exist so the
 * property tests can compare cstdx behaviour against the standard library.
 */

#include <cstdx/base64.h>
#include <cstdx/bytes.h>
#include <cstdx/string.h>
#include <cstdx/string_view.h>
#include <cstdx/vector.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cstdx_diff {
    [[noreturn]] inline void fail(const char* message) { throw std::runtime_error(message); }

    /* ------------------------------------------------------------------------- */
    /* RAII wrappers around the opaque handles.                                   */
    /* ------------------------------------------------------------------------- */

    class CxString {
    public:
        CxString() = default;
        explicit CxString(cx_string * handle) : handle_(handle) {}

        ~CxString() { cx_string_destroy(&handle_); }

        CxString(CxString && other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }

        CxString& operator = (CxString && other) noexcept {
            if (this != &other) {
                cx_string_destroy(&handle_);
                handle_ = other.handle_;
                other.handle_ = nullptr;
            }

            return *this;
        }

        CxString(const CxString&) = delete;
        CxString& operator = (const CxString&) = delete;

        [[nodiscard]] cx_string* get() const { return handle_; }
        [[nodiscard]] cx_string** out() { return &handle_; }

    private:
        cx_string* handle_ = nullptr;
    };

    class CxBytes {
    public:
        CxBytes() = default;
        explicit CxBytes(cx_bytes * handle) : handle_(handle) {}

        ~CxBytes() { cx_bytes_destroy(&handle_); }

        CxBytes(CxBytes && other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }

        CxBytes& operator = (CxBytes && other) noexcept {
            if (this != &other) {
                cx_bytes_destroy(&handle_);
                handle_ = other.handle_;
                other.handle_ = nullptr;
            }

            return *this;
        }

        CxBytes(const CxBytes&) = delete;
        CxBytes& operator = (const CxBytes&) = delete;

        [[nodiscard]] cx_bytes* get() const { return handle_; }
        [[nodiscard]] cx_bytes** out() { return &handle_; }

    private:
        cx_bytes* handle_ = nullptr;
    };

    class CxVector {
    public:
        CxVector() = default;
        explicit CxVector(cx_vector * handle) : handle_(handle) {}

        ~CxVector() { cx_vector_destroy(&handle_); }

        CxVector(CxVector && other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }

        CxVector& operator = (CxVector && other) noexcept {
            if (this != &other) {
                cx_vector_destroy(&handle_);
                handle_ = other.handle_;
                other.handle_ = nullptr;
            }

            return *this;
        }

        CxVector(const CxVector&) = delete;
        CxVector& operator = (const CxVector&) = delete;

        [[nodiscard]] cx_vector* get() const { return handle_; }
        [[nodiscard]] cx_vector** out() { return &handle_; }

    private:
        cx_vector* handle_ = nullptr;
    };

    /* ------------------------------------------------------------------------- */
    /* Factories and views.                                                       */
    /* ------------------------------------------------------------------------- */

    inline cx_string_view view_of(const std::string& value) {
        return cx_string_view{value.data(), value.size()};
    }

    inline CxString make_string(const std::string& value) {
        CxString result;
        auto status = cx_string_from_view(result.out(), view_of(value), nullptr);

        if (status != CX_OK) {
            fail("cx_string_from_view failed");
        }

        return result;
    }

    inline CxBytes make_bytes(const std::vector<uint8_t>& value) {
        CxBytes result;
        auto status = cx_bytes_from(result.out(), value.data(), value.size(), nullptr);

        if (status != CX_OK) {
            fail("cx_bytes_from failed");
        }

        return result;
    }

    inline CxVector make_int_vector(const std::vector<int>& value) {
        CxVector result;
        auto status = cx_vector_init_of(result.out(), int, nullptr, nullptr);

        if (status != CX_OK) {
            fail("cx_vector_init failed");
        }

        for (int item : value) {
            if (cx_vector_push(result.get(), &item) != CX_OK) {
                fail("cx_vector_push failed");
            }
        }

        return result;
    }

    inline std::string contents_of(const cx_string* value) {
        return std::string(cx_string_cstr(value), cx_string_length(value));
    }

    inline std::vector<uint8_t> contents_of(const cx_bytes* value) {
        auto data = cx_bytes_data(value);
        auto length = cx_bytes_length(value);
        return std::vector<uint8_t>(data, data + length);
    }

    inline std::vector<int> contents_of(const cx_vector* value) {
        std::vector<int> result;
        auto length = cx_vector_length(value);
        result.reserve(length);

        for (size_t i = 0; i < length; ++i) {
            result.push_back(cx_vector_at(value, int, i));
        }

        return result;
    }

    /* ------------------------------------------------------------------------- */
    /* Reference base64 implementation (RFC 4648, strict/canonical).              */
    /* ------------------------------------------------------------------------- */

    inline constexpr char kBase64Alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    inline std::string base64_encode(const std::vector<uint8_t>& input) {
        std::string output;
        output.reserve(((input.size() + 2) / 3) * 4);

        size_t i = 0;

        while (i + 3 <= input.size()) {
            uint32_t triple = (static_cast<uint32_t>(input[i]) << 16) |
                              (static_cast<uint32_t>(input[i + 1]) << 8) |
                              static_cast<uint32_t>(input[i + 2]);
            output.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
            output.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
            output.push_back(kBase64Alphabet[(triple >> 6) & 0x3F]);
            output.push_back(kBase64Alphabet[triple & 0x3F]);
            i += 3;
        }

        const auto remaining = input.size() - i;

        if (remaining == 1) {
            uint32_t triple = static_cast<uint32_t>(input[i]) << 16;
            output.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
            output.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
            output.push_back('=');
            output.push_back('=');
        } else if (remaining == 2) {
            uint32_t triple = (static_cast<uint32_t>(input[i]) << 16) |
                              (static_cast<uint32_t>(input[i + 1]) << 8);
            output.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
            output.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
            output.push_back(kBase64Alphabet[(triple >> 6) & 0x3F]);
            output.push_back('=');
        }

        return output;
    }

    inline bool is_base64_space(char value) {
        return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\v' ||
               value == '\f';
    }

    inline int base64_value(char value) {
        if (value >= 'A' && value <= 'Z') {
            return value - 'A';
        }

        if (value >= 'a' && value <= 'z') {
            return value - 'a' + 26;
        }

        if (value >= '0' && value <= '9') {
            return value - '0' + 52;
        }

        if (value == '+') {
            return 62;
        }

        if (value == '/') {
            return 63;
        }

        return -1;
    }

    inline std::optional<std::vector<uint8_t>> base64_decode(std::string_view input) {
        size_t data_chars = 0;
        size_t padding = 0;
        bool seen_padding = false;

        for (char value : input) {
            if (is_base64_space(value)) {
                continue;
            }

            if (value == '=') {
                seen_padding = true;

                if (++padding > 2) {
                    return std::nullopt;
                }

                continue;
            }

            if (seen_padding || base64_value(value) < 0) {
                return std::nullopt;
            }

            ++data_chars;
        }

        const auto remainder = data_chars % 4;
        size_t decoded_length = 0;

        if (padding == 0) {
            if (remainder == 1) {
                return std::nullopt;
            }

            decoded_length = (data_chars / 4) * 3 + (remainder == 2 ? 1 : remainder == 3 ? 2 : 0);
        } else if (padding == 1) {
            if (remainder != 3) {
                return std::nullopt;
            }

            decoded_length = (data_chars / 4) * 3 + 2;
        } else {
            if (remainder != 2) {
                return std::nullopt;
            }

            decoded_length = (data_chars / 4) * 3 + 1;
        }

        std::vector<uint8_t> output;
        output.reserve(decoded_length);

        uint32_t accumulator = 0;
        size_t group_length = 0;

        for (char value : input) {
            if (is_base64_space(value)) {
                continue;
            }

            if (value == '=') {
                break;
            }

            accumulator = (accumulator << 6) | static_cast<uint32_t>(base64_value(value));
            ++group_length;

            if (group_length == 4) {
                output.push_back(static_cast<uint8_t>(accumulator >> 16));
                output.push_back(static_cast<uint8_t>(accumulator >> 8));
                output.push_back(static_cast<uint8_t>(accumulator));
                accumulator = 0;
                group_length = 0;
            }
        }

        if (group_length == 2) {
            if ((accumulator & 0x0F) != 0) {
                return std::nullopt;
            }

            output.push_back(static_cast<uint8_t>(accumulator >> 4));
        } else if (group_length == 3) {
            if ((accumulator & 0x03) != 0) {
                return std::nullopt;
            }

            output.push_back(static_cast<uint8_t>(accumulator >> 10));
            output.push_back(static_cast<uint8_t>(accumulator >> 2));
        } else if (group_length == 1) {
            return std::nullopt;
        }

        if (output.size() != decoded_length) {
            return std::nullopt;
        }

        return output;
    }

} // namespace cstdx_diff

#endif /* CSTDX_TESTS_CPP_DIFF_COMMON_H */
