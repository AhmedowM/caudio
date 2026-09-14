module;
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>

/**
 * @file error.cppm
 * @brief Error type, Expected alias and helpers.
 * @ingroup caudio_utils
 */

export module caudio.utils:error;

import :result;

export namespace caudio::utils {

/**
 * @brief Carries a StatusCode plus an optional diagnostic message.
 * @ingroup caudio_utils
 * @details Intended as the error type for `std::expected<T, Error>`.
 * Equality is defaulted; message is owned (`std::string`) so the view
 * passed to the constructor is copied.
 * @see StatusCode
 * @see makeError
 * @see Expected
 */
struct Error {
    StatusCode code{StatusCode::Ok}; ///< Machine-readable error code.
    std::string message{};           ///< Human-readable context; may be empty.

    /**
     * @brief Constructs a default success error (code Ok, empty message).
     * @ingroup caudio_utils
     */
    Error() noexcept = default;

    /**
     * @brief Constructs an error with code and message view.
     * @ingroup caudio_utils
     * @param c Status code.
     * @param msg Diagnostic message view (copied).
     */
    explicit Error(StatusCode c, std::string_view msg) : code(c), message(msg) {}

    /**
     * @brief Deprecated C-string overload.
     * @ingroup caudio_utils
     * @param c Status code.
     * @param msg Null-terminated message; null is treated as empty.
     * @deprecated Use string_view overload.
     */
    [[deprecated("use string_view overload")]]
    Error(StatusCode c, const char* msg)
        : code(c), message(msg ? msg : "") {}

    /**
     * @brief Equality comparison (compares code and message).
     * @ingroup caudio_utils
     * @param other Error to compare.
     * @return true if code and message are equal.
     */
    bool operator==(const Error&) const = default;
};

/**
 * @brief Alias for `std::expected<T, Error>`.
 * @ingroup caudio_utils
 * @tparam T Value type on success.
 * @details Use with `std::unexpected(Error{...})` on failure. Error codes
 * are documented per-function; common codes are InvalidArg, Busy, State,
 * Unsupported and Internal.
 * @see Error
 * @see StatusCode
 */
template <typename T>
using Expected = std::expected<T, Error>;

/**
 * @brief Helper to construct an Error.
 * @ingroup caudio_utils
 * @param c Status code.
 * @param msg Optional diagnostic view (default empty).
 * @return Error instance with code and copied message.
 * @see Error
 */
inline Error makeError(StatusCode c, std::string_view msg = {}) {
    return Error{c, msg};
}

} // namespace caudio::utils

/**
 * @brief std::formatter specialization for Error.
 * @details Formats as `"<Code>[: message]"`, e.g. `Busy: queue full`.
 */
template <>
struct std::formatter<caudio::utils::Error> : std::formatter<std::string> {
    auto format(const caudio::utils::Error& e, auto& ctx) const {
        std::string s = std::string(caudio::utils::toString(e.code));
        if (!e.message.empty()) {
            s += ": ";
            s += e.message;
        }
        return std::formatter<std::string>::format(s, ctx);
    }
};
