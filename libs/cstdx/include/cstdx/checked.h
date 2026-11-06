#ifndef CSTDX_CHECKED_H
#define CSTDX_CHECKED_H

/**
 * @file checked.h
 * @brief Checked integer arithmetic and alignment helpers.
 *
 * All functions are total: they never invoke undefined behavior and report
 * failure through their return value. They are implemented with portable C,
 * not compiler builtins.
 */

#include <stddef.h>

#include <cstdx/api.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute @p a + @p b, detecting overflow.
 *
 * @param[out] out Receives the sum on success. Untouched on overflow.
 * @return @c true if the sum does not fit in @c size_t.
 */
[[nodiscard]] CX_API bool cx_add_overflow(size_t a, size_t b, size_t *out);

/**
 * @brief Compute @p a * @p b, detecting overflow.
 *
 * @param[out] out Receives the product on success. Untouched on overflow.
 * @return @c true if the product does not fit in @c size_t.
 */
[[nodiscard]] CX_API bool cx_mul_overflow(size_t a, size_t b, size_t *out);

/**
 * @brief Test whether @p value is a nonzero power of two.
 */
[[nodiscard]] CX_API bool cx_is_power_of_two(size_t value);

/**
 * @brief Round @p value up to the next multiple of @p alignment.
 *
 * @p alignment must be a nonzero power of two.
 *
 * @param[out] out Receives the rounded value on success.
 * @return @c true on success, @c false if @p alignment is invalid or the
 *         rounded value does not fit in @c size_t.
 */
[[nodiscard]] CX_API bool cx_align_up(size_t value, size_t alignment,
                                      size_t *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CSTDX_CHECKED_H */
