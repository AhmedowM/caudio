#pragma once

#include <caudio/db/database.hpp>
#include <caudio/engine/engine.hpp>
#include <caudio/utils/error.hpp>
#include <expected>
#include <memory>
#include <string_view>
#include <utility>

namespace caudio::test_helpers {

inline std::expected<std::unique_ptr<caudio::engine::Engine>, caudio::utils::Error>
openEngineWithDatabase(std::string_view path, const caudio::engine::EngineConfig& config) {
    auto database = caudio::db::Database::open(path);
    if (!database)
        return std::unexpected(database.error());

    auto engine = caudio::engine::Engine::create(config);
    if (!engine)
        return std::unexpected(engine.error());

    auto instance = std::move(engine.value());
    std::shared_ptr<caudio::db::Database> sharedDatabase(std::move(database.value()));
    if (auto attached = instance->attachDatabase(std::move(sharedDatabase)); !attached)
        return std::unexpected(attached.error());
    return instance;
}

} // namespace caudio::test_helpers
