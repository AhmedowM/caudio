module;
#include <string_view>

export module caudio.db;

import caudio.utils;

export namespace caudio::db {

constexpr std::string_view toString(caudio::utils::Result r) noexcept {
  return caudio::utils::toString(r);
}

} // namespace caudio::db
