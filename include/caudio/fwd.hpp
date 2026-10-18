#pragma once

/**
 * @file fwd.hpp
 * @brief Shared forward declarations used across caudio public headers.
 * @ingroup caudio
 * @details Centralizes forward declarations so that multiple public headers
 * do not repeat the same `struct sqlite3;` / `class Logger;` / `class Database;`
 * blocks. Include this instead of re-declaring in each header.
 */

#include <type_traits>

struct sqlite3;
struct sqlite3_stmt;

namespace caudio::config {
struct Config;
} // namespace caudio::config

namespace caudio::db {
class Database;
struct Track;
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
enum class PlaybackState : int;
enum class RepeatMode : int;
enum class EngineEventType : int;
} // namespace caudio::engine

namespace caudio::player {
class Reader;
class Decoder;
class AudioOutput;
} // namespace caudio::player

namespace caudio::service {
class IpcServer;
class Service;
class ShmStatusHandle;
} // namespace caudio::service

namespace caudio::ipc {
struct Status;
} // namespace caudio::ipc

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
class Logger;
class Json;
class JsonRef;
struct Error;
} // namespace caudio::utils
