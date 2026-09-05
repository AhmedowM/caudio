module;
#include <string_view>

export module caudio.utils:result;

export namespace caudio::utils {

enum class Result : int {
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

constexpr std::string_view toString(Result r) noexcept {
    switch (r) {
    case Result::Ok:
        return "Ok";
    case Result::InvalidArg:
        return "InvalidArg";
    case Result::NotFound:
        return "NotFound";
    case Result::Unsupported:
        return "Unsupported";
    case Result::Io:
        return "Io";
    case Result::Device:
        return "Device";
    case Result::State:
        return "State";
    case Result::NoMem:
        return "NoMem";
    case Result::Internal:
        return "Internal";
    case Result::AlreadyExists:
        return "AlreadyExists";
    case Result::Busy:
        return "Busy";
    case Result::Corrupt:
        return "Corrupt";
    case Result::NoSpace:
        return "NoSpace";
    default:
        return "Unknown";
    }
}

} // namespace caudio::utils
