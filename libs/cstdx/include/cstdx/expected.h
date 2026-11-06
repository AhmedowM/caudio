/**
 * @file expected.h
 * @brief A result type containing either a value or an error.
 * @details This is a C equivalent of the core idea behind C++ std::expected.
 * The stored value is an opaque, non-owning pointer. The caller must ensure
 * that the pointed-to object remains valid for as long as it is used.
 */

#ifndef CSTDX_EXPECTED_H
#define CSTDX_EXPECTED_H

#include <cstdx/api.h>
#include <cstdx/error.h>

/**
 * @brief A value-or-error result.
 * @details Only the union member corresponding to the current state is valid.
 * Use cx_expected_has_value() or cx_expected_has_error() before accessing a
 * payload. The pointed-to value and error message are not owned by this type.
 */
typedef struct cx_expected {
    /** Internal state; use the query functions instead of inspecting it. */
    enum {
        CX_EXPECTED_VALUE,
        CX_EXPECTED_ERROR
    } state;

    union {
        /** Non-owning result pointer, valid when state is CX_EXPECTED_VALUE. */
        void* value;

        /** Error payload, valid when state is CX_EXPECTED_ERROR. */
        cx_error error;
    } data;
} cx_expected;

/**
 * @brief Construct a successful result.
 * @param value Non-owning pointer to the successful result, which may be nullptr.
 * @return A result whose state is CX_EXPECTED_VALUE.
 */
[[nodiscard]] CX_API cx_expected cx_expected_from_value(void* value);

/**
 * @brief Construct a failed result.
 * @param error Error value to store. The message pointer is not copied.
 * @return A result whose state is CX_EXPECTED_ERROR.
 */
[[nodiscard]] CX_API cx_expected cx_expected_from_error(cx_error error);

/**
 * @brief Test whether a result contains a value.
 * @param expected Result to inspect. nullptr is treated as false.
 * @return true when the result contains a value; otherwise false.
 */
[[nodiscard]] CX_API bool cx_expected_has_value(const cx_expected* expected);

/**
 * @brief Test whether a result contains an error.
 * @param expected Result to inspect. nullptr is treated as false.
 * @return true when the result contains an error; otherwise false.
 */
[[nodiscard]] CX_API bool cx_expected_has_error(const cx_expected* expected);

/**
 * @brief Get the stored value pointer.
 * @param expected Result to inspect.
 * @return The stored value, or nullptr when expected is nullptr or contains an error.
 * @note A nullptr return does not distinguish an absent value from a successful
 * result that intentionally stores nullptr. Check cx_expected_has_value() first.
 */
[[nodiscard]] CX_API void* cx_expected_value(const cx_expected* expected);

/**
 * @brief Get the stored error.
 * @param expected Result to inspect.
 * @return A pointer to the stored error, or nullptr when expected is nullptr or
 * contains a value. The returned pointer is owned by expected.
 */
[[nodiscard]] CX_API const cx_error* cx_expected_error(const cx_expected* expected);

#endif // CSTDX_EXPECTED_H
