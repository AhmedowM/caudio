module;
#include <format>
#include <string_view>

 /**
  * @file result.cppm
  * @brief Status codes and string conversion for caudio utilities.
  * @ingroup caudio_utils
  * @defgroup caudio_utils caudio utilities
  * @brief Core utility types for error handling, logging, threading and queues.
  *
  * @details The caudio_utils module group aggregates lightweight, header-like
  * C++23 module partitions that have no external runtime dependencies beyond
  * the standard library. All public symbols are exported under
  * `caudio::utils` and use @ref StatusCode / @ref Error for uniform error
  * reporting via `std::expected`.
  */

export module caudio.utils:result;

export namespace caudio::utils {

/**
 * @brief Status codes used across caudio for `std::expected` error reporting.
 * @ingroup caudio_utils
 * @details Mirrors the C API `ca_status` values. `Ok` (0) indicates success;
 * all other values indicate failure. Pass to toString() for diagnostics or
 * construct an Error with makeError().
 * @see Error
 * @see toString
 */
enum class StatusCode : int {
    Ok = 0,            ///< Success.
    InvalidArg = 1,    ///< Invalid argument supplied by caller.
    NotFound = 2,      ///< Requested entity not found.
    Unsupported = 3,   ///< Operation not supported on this platform/build.
    Io = 4,            ///< Generic I/O error.
    Device = 5,        ///< Audio device error.
    State = 6,         ///< Invalid state for operation (e.g. queue empty).
    NoMem = 7,         ///< Memory allocation failure.
    Internal = 8,      ///< Internal invariant violation.
    AlreadyExists = 9, ///< Entity already exists.
    Busy = 10,         ///< Resource busy or queue full.
    Corrupt = 11,      ///< Data corruption detected.
    NoSpace = 12       ///< No space left (buffer/queue full).
};

/**
 * @brief Converts a StatusCode to a human-readable string view.
 * @ingroup caudio_utils
 * @param r Status code to stringify.
 * @return Non-owning string view; `"Unknown"` for out-of-range values.
 * @see StatusCode
 */
constexpr std::string_view toString(StatusCode r) noexcept {
    using enum StatusCode;
    switch (r) {
    case Ok:
        return "Ok";
    case InvalidArg:
        return "InvalidArg";
    case NotFound:
        return "NotFound";
    case Unsupported:
        return "Unsupported";
    case Io:
        return "Io";
    case Device:
        return "Device";
    case State:
        return "State";
    case NoMem:
        return "NoMem";
    case Internal:
        return "Internal";
    case AlreadyExists:
        return "AlreadyExists";
    case Busy:
        return "Busy";
    case Corrupt:
        return "Corrupt";
    case NoSpace:
        return "NoSpace";
    default:
        return "Unknown";
    }
}

} // namespace caudio::utils

/**
 * @brief std::formatter specialization for StatusCode.
 * @details Enables `std::format("{}", StatusCode::Busy)` via toString().
 */
template <>
struct std::formatter<caudio::utils::StatusCode> : std::formatter<std::string_view> {
    auto format(caudio::utils::StatusCode r, auto& ctx) const {
        return std::formatter<std::string_view>::format(caudio::utils::toString(r), ctx);
    }
};
