#pragma once

/**
 * @file fwd.hpp
 * @brief Shared forward declarations and the canonical `Expected<T>` alias.
 * @ingroup caudio
 * @details Centralizes forward declarations so that multiple public headers
 * do not repeat the same `struct sqlite3;` / `class Logger;` / `class Database;`
 * blocks. Include this instead of re-declaring in each header. Also defines the
 * single canonical `caudio::utils::Expected<T>` alias so public headers can name
 * fallible return types without pulling the heavier `utils/error.hpp` (which
 * drags `<format>`, `<string>` and the `Error` formatter).
 * @include-only This header is fully inline; no .cpp is required.
 */

#include <cstdint>
#include <expected>
#include <type_traits>

struct sqlite3;
struct sqlite3_stmt;

namespace caudio::config {
struct Config;
} // namespace caudio::config

namespace caudio::db {
class Database;
struct DbStats;
class SqliteStatement;
} // namespace caudio::db

namespace caudio::engine {
class Engine;
struct EngineConfig;
struct EngineCallbacks;
struct EngineEvent;
enum class EngineEventType : std::uint8_t;
// PlaybackState / RepeatMode are wire enums owned by `caudio::types` (see
// types/wire.hpp); `caudio::engine` re-exports them as aliases (engine/types.hpp).
} // namespace caudio::engine

namespace caudio::media {
class Reader;
class Decoder;
} // namespace caudio::media

namespace caudio::player {
using Reader = caudio::media::Reader;
using Decoder = caudio::media::Decoder;
class AudioOutput;
} // namespace caudio::player

namespace caudio::service {
// IpcServer is internal (src/service/ipc_server.hpp); the public
// `caudio/service/service.hpp` only names it in Service's private members,
// so the forward declaration stays here.
class IpcServer;
class Service;
} // namespace caudio::service

namespace caudio::ipc {
struct Status;
class ShmStatusHandle;
class ShmStatusReader;
class ShmStatusWriter;
} // namespace caudio::ipc

namespace caudio::types {
enum class RepeatMode : std::uint8_t;
enum class PlaybackState : std::uint8_t;
struct Track;
struct Playlist;
struct QueueItem;
struct HistoryEntry;
struct Bookmark;
} // namespace caudio::types

namespace caudio::utils {
template <typename T>
    requires std::is_trivially_copyable_v<T>
class SpscRing;
template <typename T>
class Generator;
class Logger;
class Json;
class JsonRef;
enum class StatusCode : std::uint8_t;
struct Error;

/**
 * @brief Canonical alias for `std::expected<T, Error>`.
 * @ingroup caudio
 * @tparam T Value type on success.
 * @details Defined here (single definition) so public headers can name
 * fallible return types as `Expected<T>` with only `fwd.hpp`, without
 * including `utils/error.hpp`. `Error` may be incomplete at any declaration
 * that merely returns or takes `Expected<T>`; only definitions need it whole.
 */
template <typename T>
using Expected = std::expected<T, Error>;
} // namespace caudio::utils
