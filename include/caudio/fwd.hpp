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
 */

#include <expected>
#include <type_traits>

struct sqlite3;
struct sqlite3_stmt;

namespace caudio::config {
struct Config;
} // namespace caudio::config

namespace caudio::db {
class Database;
struct Track;
struct Playlist;
struct HistoryEntry;
struct DbStats;
class SqliteStatement;
struct LibraryStatsDetailedData;
} // namespace caudio::db

namespace caudio::engine {
class Engine;
struct EngineConfig;
struct EngineCallbacks;
struct EngineEvent;
struct EngineState;
struct QueueState;
enum class EngineEventType : int;
// PlaybackState / RepeatMode are wire enums owned by `caudio::types` (see
// types/wire.hpp); `caudio::engine` re-exports them as aliases (engine/types.hpp).
} // namespace caudio::engine

namespace caudio::player {
class Reader;
class Decoder;
class AudioOutput;
} // namespace caudio::player

namespace caudio::service {
class IpcServer;
class Service;
} // namespace caudio::service

namespace caudio::ipc {
struct Status;
class ShmStatusHandle;
} // namespace caudio::ipc

namespace caudio::types {
enum class RepeatMode : int;
enum class PlaybackState : int;
} // namespace caudio::types

namespace caudio::client {
class IpcClient;
class OutputFormatter;
} // namespace caudio::client

namespace caudio::utils {
template <typename T>
class MpscQueue;
template <typename T>
    requires std::is_trivially_copyable_v<T>
class SpscRing;
template <typename T>
class Generator;
template <typename Sig>
class MoveOnlyFunction;
class Logger;
class Json;
class JsonRef;
enum class StatusCode : int;
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
