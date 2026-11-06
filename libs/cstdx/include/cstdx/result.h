/**
 * @file result.h
 * @brief A result type containing either success or an error.
 * @details Use cx_result for operations whose successful completion does not
 * produce a value. Use cx_expected when success also carries a value.
 */

#ifndef CSTDX_RESULT_H
#define CSTDX_RESULT_H

#include <cstdx/api.h>
#include <cstdx/error.h>

/**
 * @brief A success-or-error result without a success payload.
 * @details The error member is valid only when the result represents failure.
 * The error message is not copied and must remain valid while it is inspected.
 */
typedef struct cx_result {
    enum {
        CX_RESULT_SUCCESS,
        CX_RESULT_FAILURE
    } state;

    /** Error payload, valid only when state is CX_RESULT_FAILURE. */
    cx_error error;
} cx_result;

/**
 * @brief Construct a successful result.
 * @return A result whose state is CX_RESULT_SUCCESS.
 */
[[nodiscard]] CX_API cx_result cx_result_success(void);

/**
 * @brief Construct a failed result.
 * @param error Error value to store. The message pointer is not copied.
 * @return A result whose state is CX_RESULT_FAILURE.
 */
[[nodiscard]] CX_API cx_result cx_result_failure(cx_error error);

/**
 * @brief Test whether an operation succeeded.
 * @param result Result to inspect. nullptr is treated as false.
 * @return true when result is non-null and successful; otherwise false.
 */
[[nodiscard]] CX_API bool cx_result_is_success(const cx_result* result);

/**
 * @brief Test whether an operation failed.
 * @param result Result to inspect. nullptr is treated as false.
 * @return true when result is non-null and failed; otherwise false.
 */
[[nodiscard]] CX_API bool cx_result_is_failure(const cx_result* result);

/**
 * @brief Get the error from a failed result.
 * @param result Result to inspect.
 * @return A pointer to the stored error, or nullptr when result is null or
 * successful. The returned pointer is owned by result.
 */
[[nodiscard]] CX_API const cx_error* cx_result_error(const cx_result* result);

#endif // CSTDX_RESULT_H
