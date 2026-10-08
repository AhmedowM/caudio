module;
#include <caudio/utils/error.hpp>

export module caudio.utils:error;

export namespace caudio::utils {
using ::caudio::utils::Error;
using ::caudio::utils::Expected;
using ::caudio::utils::makeError;
} // namespace caudio::utils
