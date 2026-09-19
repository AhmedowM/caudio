module;
#include "caudio/utils/thread.hpp"

export module caudio.utils:thread;

export namespace caudio::utils {
  using ::caudio::utils::sleepFor;
  using ::caudio::utils::sleepForMs;
  using ::caudio::utils::setThreadName;
}
