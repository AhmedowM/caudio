module;
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>

export module caudio.utils:error;

import :result;

export namespace caudio::utils {

struct Error {
    StatusCode code{StatusCode::Ok};
    std::string message{};

    Error() noexcept = default;
    explicit Error(StatusCode c, std::string_view msg) : code(c), message(msg) {}
    [[deprecated("use string_view overload")]]
    Error(StatusCode c, const char* msg)
        : code(c), message(msg ? msg : "") {}

    bool operator==(const Error&) const = default;
};

template <typename T>
using Expected = std::expected<T, Error>;

inline Error makeError(StatusCode c, std::string_view msg = {}) {
    return Error{c, msg};
}

} // namespace caudio::utils

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
