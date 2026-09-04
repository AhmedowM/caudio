module;
#include <string_view>

export module caudio.engine;

import caudio.utils;
import caudio.player;
import caudio.db;

export namespace caudio::engine {

constexpr std::string_view toString(caudio::utils::Result r) noexcept {
  return caudio::utils::toString(r);
}

} // namespace caudio::engine
