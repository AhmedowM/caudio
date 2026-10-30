#pragma once

#include <caudio/db/database.hpp>
#include <caudio/engine/engine.hpp>
#include <caudio/service/service.hpp>
#include <caudio/utils/error.hpp>
#include <expected>
#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>

namespace caudio::test_helpers {

inline std::expected<std::unique_ptr<caudio::engine::Engine>, caudio::utils::Error>
openEngineWithDatabase(std::string_view path, const caudio::types::EngineConfig& config) {
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
inline std::expected<std::unique_ptr<caudio::service::Service>, caudio::utils::Error>
openServiceWithDatabase(std::string_view path, const caudio::service::ServiceConfig& config) {
    auto database = caudio::db::Database::open(path);
    if (!database)
        return std::unexpected(database.error());

    std::shared_ptr<caudio::contracts::IDatabase> sharedDatabase(std::move(database.value()));
    return caudio::service::Service::create(config, std::move(sharedDatabase));
}

} // namespace caudio::test_helpers
