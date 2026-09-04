module;
#include <string_view>

export module caudio.player;

import caudio.utils;

export namespace caudio::player {

constexpr std::string_view toString(caudio::utils::Result r) noexcept {
  return caudio::utils::toString(r);
}

} // namespace caudio::player
