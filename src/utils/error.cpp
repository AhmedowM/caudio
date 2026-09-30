#include <caudio/utils/error.hpp>
#include <caudio/utils/result.hpp>
#include <string_view>

namespace caudio::utils {

Error::Error(StatusCode c, std::string_view msg) : code(c), message(msg) {}

Error makeError(StatusCode c, std::string_view msg) {
    return Error{c, msg};
}

} // namespace caudio::utils
