module;
#include <string_view>

export module caudio.utils;

export namespace caudio::utils {

enum class Result {
  Ok,
  InvalidArg,
  NotFound,
  Unsupported,
  Io,
  Device,
  State,
  NoMem,
  Internal,
  AlreadyExists,
  Busy,
  Corrupt,
  NoSpace
};

constexpr std::string_view toString(Result r) noexcept {
  switch (r) {
    case Result::Ok: return "Ok";
    case Result::InvalidArg: return "InvalidArg";
    case Result::NotFound: return "NotFound";
    case Result::Unsupported: return "Unsupported";
    case Result::Io: return "Io";
    case Result::Device: return "Device";
    case Result::State: return "State";
    case Result::NoMem: return "NoMem";
    case Result::Internal: return "Internal";
    case Result::AlreadyExists: return "AlreadyExists";
    case Result::Busy: return "Busy";
    case Result::Corrupt: return "Corrupt";
    case Result::NoSpace: return "NoSpace";
    default: return "Unknown";
  }
}

} // namespace caudio::utils
