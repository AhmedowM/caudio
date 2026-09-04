module;
#include <string>
#include <string_view>
#include <expected>
#include <utility>

export module caudio.utils:error;

import :result;

export namespace caudio::utils {

struct Error {
  Result code{Result::Ok};
  std::string message{};

  Error() noexcept = default;
  Error(Result c, std::string msg) : code(c), message(std::move(msg)) {}
  Error(Result c, std::string_view msg) : code(c), message(msg) {}
  Error(Result c, const char* msg) : code(c), message(msg ? msg : "") {}
};

template <typename T>
using Expected = std::expected<T, Error>;

inline Error makeError(Result c, std::string_view msg = {}) {
  return Error{c, std::string(msg)};
}

} // namespace caudio::utils
