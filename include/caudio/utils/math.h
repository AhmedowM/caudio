#ifndef CAUDIO_UTILS_MATH_H
#define CAUDIO_UTILS_MATH_H

/**
 * @file caudio/utils/math.h
 * @brief Miscellaneous math utilities.
 * @ingroup caudio_utils
 * @note This header is fully inline; no .c is required.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>

#include <cstdx/allocator.h>
#include <cstdx/string.h>
#include <cstdx/string_view.h>

#include <stdint.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Clamps a volume value to the valid range [0.0, 1.0].
 *
 * @param v Volume value to clamp.
 * @return Clamped volume; 0.0f for NaN/infinity.
 */
[[nodiscard]] static inline float ca_clamp_volume(float v) {
    if (!isfinite(v))
        return 0.0f;
    if (v < 0.0f)
        return 0.0f;
    if (v > 1.0f)
        return 1.0f;
    return v;
}

/**
 * @brief Converts 32 bytes to a 64-char lowercase hex string.
 *
 * @param bytes Input bytes (32 bytes).
 * @param alloc Allocator for the output string, or nullptr for default.
 * @param out Receives a newly allocated cx_string handle on success.
 * @return CA_OK on success, CA_OUT_OF_MEMORY on allocation failure.
 */
[[nodiscard]] CAUDIO_API ca_status ca_to_hex(const uint8_t bytes[32],
                                  const cx_allocator* alloc,
                                  cx_string** out);

/**
 * @brief Parses a 64-char hex string into 32 bytes.
 *
 * @param hex_str Hex view (must be 64 chars, case-insensitive).
 * @param out Output bytes (32 bytes).
 * @return true on success, false on length/character error.
 */
[[nodiscard]] CAUDIO_API bool ca_from_hex(cx_string_view hex_str, uint8_t out[32]);

/**
 * @brief Formats seconds as mm:ss or hh:mm:ss.
 *
 * @param secs Position or duration in seconds (negative clamps to 0).
 * @param alloc Allocator for the output string, or nullptr for default.
 * @param out Receives a newly allocated cx_string handle on success.
 * @return CA_OK on success, CA_OUT_OF_MEMORY on allocation failure.
 */
[[nodiscard]] CAUDIO_API ca_status ca_format_clock(double secs,
                                        const cx_allocator* alloc,
                                        cx_string** out);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_UTILS_MATH_H */
