module;
#include <string_view>

export module caudio.db;

export import :types;
export import :schema;
export import :database;
export import :scan;
export import :search;
export import :json;
export import :write_thread;

import caudio.utils;

export namespace caudio::db {

constexpr std::string_view toString(caudio::utils::Result r) noexcept {
  return caudio::utils::toString(r);
}

} // namespace caudio::db
