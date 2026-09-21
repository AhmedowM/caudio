module;
#include "caudio/utils/ring.hpp"

export module caudio.utils:ring;

export namespace caudio::utils {
using ::caudio::utils::kRingCacheLine;
using ::caudio::utils::SpscRing;
} // namespace caudio::utils
