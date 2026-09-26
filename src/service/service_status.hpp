#pragma once

/**
 * @file service_status.hpp
 * @brief Daemon status snapshot builder for caudio.service (internal).
 * @ingroup caudio_service
 */

#include <caudio/ipc/result.hpp>
#include <caudio/utils.hpp>
#include <expected>

namespace caudio::engine {
class Engine;
} // namespace caudio::engine

namespace caudio::db {
class Database;
} // namespace caudio::db

namespace caudio::service::detail {

/**
 * @brief Build a Status object from Engine and Database.
 * Populates playback state, position, duration, volume, shuffle, repeat,
 * current track info (title, artist, path), and active queue size/index.
 * @param eng Engine reference.
 * @param db Database reference.
 * @return Status on success, Error on failure.
 */
std::expected<caudio::ipc::Status, caudio::utils::Error> buildStatus(caudio::engine::Engine& eng,
                                                                     caudio::db::Database& db);

} // namespace caudio::service::detail
