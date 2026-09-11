module;
#include <format>
#include <string_view>

export module caudio.utils:result;

export namespace caudio::utils {

enum class StatusCode : int {
    Ok = 0,
    InvalidArg = 1,
    NotFound = 2,
    Unsupported = 3,
    Io = 4,
    Device = 5,
    State = 6,
    NoMem = 7,
    Internal = 8,
    AlreadyExists = 9,
    Busy = 10,
    Corrupt = 11,
    NoSpace = 12
};

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

template <>
struct std::formatter<caudio::utils::StatusCode> : std::formatter<std::string_view> {
    auto format(caudio::utils::StatusCode r, auto& ctx) const {
        return std::formatter<std::string_view>::format(caudio::utils::toString(r), ctx);
    }
};




