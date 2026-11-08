#ifndef CSTDX_BASE64_H
#define CSTDX_BASE64_H

/**
 * @file base64.h
 * @brief Base64 encode/decode (RFC 4648, standard alphabet).
 *
 * Encoding uses the standard `A-Z a-z 0-9 + /` alphabet with `=` padding.
 * Decoding ignores ASCII whitespace, accepts input with or without padding,
 * and rejects malformed or non-canonical input (including non-zero unused
 * trailing bits).
 */

#include <cstdx/allocator.h>
#include <cstdx/api.h>
#include <cstdx/bytes.h>
#include <cstdx/slice.h>
#include <cstdx/status.h>
#include <cstdx/string.h>
#include <cstdx/string_view.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute the encoded length of @p input_length bytes.
 *
 * @param out Receives `ceil(input_length / 3) * 4`.
 * @return `CX_OK`, `CX_INVALID_ARGUMENT`, or `CX_OVERFLOW`.
 */
[[nodiscard]] CX_API cx_status cx_base64_encoded_length(size_t input_length, size_t* out);

/**
 * @brief Encode a byte slice as base64.
 *
 * @param input Bytes to encode; may be empty.
 * @param[out] out Receives a newly allocated string handle on success. Set
 *                 to nullptr on failure.
 * @param allocator Allocator to use, or nullptr for the default allocator.
 */
[[nodiscard]] CX_API cx_status cx_base64_encode(cx_slice input, cx_string** out,
                                                const cx_allocator* allocator);

/**
 * @brief Decode base64 text into a byte buffer.
 *
 * @param input Text to decode; may contain ASCII whitespace.
 * @param[out] out Receives a newly allocated buffer handle on success. Set
 *                 to nullptr on failure.
 * @param allocator Allocator to use, or nullptr for the default allocator.
 *
 * @return `CX_OK`, `CX_INVALID_ARGUMENT` for malformed input, or an
 *         allocation/overflow status.
 */
[[nodiscard]] CX_API cx_status cx_base64_decode(cx_string_view input, cx_bytes** out,
                                                const cx_allocator* allocator);

/**
 * @brief Decode a NUL-terminated base64 string.
 */
[[nodiscard]] CX_API cx_status cx_base64_decode_cstr(const char* input, cx_bytes** out,
                                                     const cx_allocator* allocator);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_BASE64_H */
