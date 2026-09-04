#include <string>
#include <string_view>
import caudio.utils;

namespace caudio::utils::test {

bool result_toString_all() {
  if (std::string(toString(Result::Ok)).compare("Ok") != 0) return false;
  if (std::string(toString(Result::InvalidArg)).compare("InvalidArg") != 0) return false;
  if (std::string(toString(Result::NotFound)).compare("NotFound") != 0) return false;
  if (std::string(toString(Result::Unsupported)).compare("Unsupported") != 0) return false;
  if (std::string(toString(Result::Io)).compare("Io") != 0) return false;
  if (std::string(toString(Result::Device)).compare("Device") != 0) return false;
  if (std::string(toString(Result::State)).compare("State") != 0) return false;
  if (std::string(toString(Result::NoMem)).compare("NoMem") != 0) return false;
  if (std::string(toString(Result::Internal)).compare("Internal") != 0) return false;
  if (std::string(toString(Result::AlreadyExists)).compare("AlreadyExists") != 0) return false;
  if (std::string(toString(Result::Busy)).compare("Busy") != 0) return false;
  if (std::string(toString(Result::Corrupt)).compare("Corrupt") != 0) return false;
  if (std::string(toString(Result::NoSpace)).compare("NoSpace") != 0) return false;
  if (std::string(toString(static_cast<Result>(99))).compare("Unknown") != 0) return false;
  return true;
}

bool result_enum_sequential() {
  if (static_cast<int>(Result::Ok) != 0) return false;
  if (static_cast<int>(Result::InvalidArg) != 1) return false;
  if (static_cast<int>(Result::NotFound) != 2) return false;
  if (static_cast<int>(Result::Unsupported) != 3) return false;
  if (static_cast<int>(Result::Io) != 4) return false;
  if (static_cast<int>(Result::Device) != 5) return false;
  if (static_cast<int>(Result::State) != 6) return false;
  if (static_cast<int>(Result::NoMem) != 7) return false;
  if (static_cast<int>(Result::Internal) != 8) return false;
  if (static_cast<int>(Result::AlreadyExists) != 9) return false;
  if (static_cast<int>(Result::Busy) != 10) return false;
  if (static_cast<int>(Result::Corrupt) != 11) return false;
  if (static_cast<int>(Result::NoSpace) != 12) return false;
  return true;
}

bool result_noexcept_check() {
  static_assert(noexcept(toString(Result::Ok)));
  return noexcept(toString(Result::Ok));
}

} // namespace caudio::utils::test
