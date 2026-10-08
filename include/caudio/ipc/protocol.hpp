#pragma once

/**
 * @file protocol.hpp
 * @brief IPC protocol implementation for caudio CLI communication.
 * @ingroup caudio_ipc
 *
 * Provides JSON serialization/deserialization for commands and results,
 * IPC request/reply framing, and the wire protocol implementation.
 *
 * ## IPC Protocol Structure
 *
 * All communication between CLI clients and the caudio daemon uses a
 * length-prefixed JSON format over a Unix domain socket (Linux/macOS) or
 * named pipe (Windows).
 *
 * ### Wire Format
 *
 * Each message consists of:
 * - 4-byte big-endian length prefix (uint32_t) - the size of the JSON payload in bytes
 * - JSON payload (UTF-8 encoded)
 *
 * ### Request Format (Client -> Daemon)
 *
 * ```json
 * {
 *   "id": 1,
 *   "cmd": {
 *     "type": "Play"
 *   }
 * }
 * ```
 *
 * ### Reply Format (Daemon -> Client)
 *
 * Success:
 * ```json
 * {
 *   "id": 1,
 *   "ok": true,
 *   "result": {
 *     "type": "Status",
 *     "state": "Playing",
 *     ...
 *   }
 * }
 * ```
 *
 * Error:
 * ```json
 * {
 *   "id": 1,
 *   "ok": false,
 *   "error": {
 *     "type": "Error",
 *     "code": "NotFound",
 *     "code_value": 2,
 *     "message": "Track not found"
 *   }
 * }
 * ```
 *
 * @see caudio::ipc::Command for command types
 * @see caudio::ipc::Result for result types
 */

#include <caudio/db/types.hpp>
#include <caudio/engine/types.hpp>
#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/json.hpp>
#include <caudio/utils/result.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace caudio::ipc {

// JSON vocabulary: the opaque caudio::utils::Json (nlohmann stays in src/).
// Track conversion lives in caudio::db (db/json.hpp) -- single definition,
// no ipc::detail duplicate.
using Json = caudio::utils::Json;

/**
 * @struct IpcRequest
 * @brief IPC request message sent from client to daemon.
 * @ingroup caudio_ipc
 *
 * @param id Request identifier for matching replies.
 * @param cmd Command to execute.
 */
struct IpcRequest final {
    uint32_t id{0};
    Command cmd{};
};

/**
 * @struct IpcReply
 * @brief IPC reply message sent from daemon to client.
 * @ingroup caudio_ipc
 *
 * @param id Request identifier (matches the request).
 * @param result Result value on success, or error on failure.
 */
struct IpcReply final {
    uint32_t id{0};
    std::expected<Result, caudio::utils::Error> result{};
};

// ---------------------------------------------------------------------------
// JSON helpers for enums
// ---------------------------------------------------------------------------
namespace detail {

/**
 * @brief Convert PlaybackState enum to string representation.
 * @ingroup caudio_ipc
 * @param s Playback state to convert.
 * @return String representation ("Stopped", "Ready", "Playing", "Paused", "Unknown").
 */
std::string playbackStateToString(caudio::engine::PlaybackState s);

/**
 * @brief Convert string to PlaybackState enum.
 * @ingroup caudio_ipc
 * @param sv String to parse ("Stopped", "Ready", "Playing", "Paused").
 * @return PlaybackState enum value, or error if unknown.
 */
std::expected<caudio::engine::PlaybackState, caudio::utils::Error>
playbackStateFromString(std::string_view sv);

/**
 * @brief Convert RepeatMode enum to string representation.
 * @ingroup caudio_ipc
 * @param m Repeat mode to convert.
 * @return String representation ("Off", "Queue", "One", "Unknown").
 */
std::string repeatModeToString(caudio::engine::RepeatMode m);

/**
 * @brief Convert string to RepeatMode enum.
 * @ingroup caudio_ipc
 * @param sv String to parse ("Off", "Queue", "One").
 * @return RepeatMode enum value, or error if unknown.
 */
std::expected<caudio::engine::RepeatMode, caudio::utils::Error>
repeatModeFromString(std::string_view sv);

/**
 * @brief Convert StatusCode enum to string representation.
 * @ingroup caudio_ipc
 * @param r Status code to convert.
 * @return String representation (e.g., "Ok", "InvalidArg", "NotFound").
 */
std::string resultCodeToString(caudio::utils::StatusCode r);

/**
 * @brief Convert string to StatusCode enum.
 * @ingroup caudio_ipc
 * @param sv String to parse.
 * @return StatusCode enum value, or error if unknown.
 */
std::expected<caudio::utils::StatusCode, caudio::utils::Error>
resultCodeFromString(std::string_view sv);

// Track JSON helpers live in caudio::db (db/json.hpp) -- single definition.
// Playlist JSON helpers
/**
 * @brief Convert Playlist struct to JSON object.
 * @ingroup caudio_ipc
 * @param p Playlist to convert.
 * @return JSON object with all playlist fields.
 */
Json playlistToJson(const caudio::db::Playlist& p);

/**
 * @brief Convert JSON object to Playlist struct.
 * @ingroup caudio_ipc
 * @param j JSON object with playlist fields.
 * @return Playlist struct, or error if parsing fails.
 */
std::expected<caudio::db::Playlist, caudio::utils::Error> playlistFromJson(const Json& j);

/**
 * @brief Convert Error struct to JSON object.
 * @ingroup caudio_ipc
 * @param e Error to convert.
 * @return JSON object with type, code, code_value, and message fields.
 */
Json errorToJson(const caudio::utils::Error& e);

/**
 * @brief Convert JSON object to Error struct.
 * @ingroup caudio_ipc
 * @param j JSON object with code/code_value and message fields.
 * @return Error struct, or error if parsing fails.
 */
std::expected<caudio::utils::Error, caudio::utils::Error> errorFromJson(const Json& j);

} // namespace detail

// ---------------------------------------------------------------------------
// Command JSON
// ---------------------------------------------------------------------------
/**
 * @brief Serialize a Command variant to JSON.
 * @ingroup caudio_ipc
 * @param cmd Command to serialize.
 * @return JSON object with "type" field and command-specific fields.
 */
Json toJson(const Command& cmd);

/**
 * @brief Deserialize a Command from JSON.
 * @ingroup caudio_ipc
 * @param j JSON object with "type" field and command-specific fields.
 * @return Command variant, or error if type is unknown or parsing fails.
 */
std::expected<Command, caudio::utils::Error> commandFromJson(const Json& j);

/**
 * @brief Deserialize a Result from JSON (forward-declared so
 * fromJson<Result> dispatches instead of erroring).
 * @ingroup caudio_ipc
 * @param j JSON object with "type" field and result-specific fields.
 * @return Result variant, or error if type is unknown or parsing fails.
 */
std::expected<Result, caudio::utils::Error> resultFromJson(const Json& j);

// Generic fromJson template wrapper: fromJson<Command>(json)
/**
 * @brief Generic template for deserializing Command or Result from JSON.
 * @ingroup caudio_ipc
 * @tparam T Type to deserialize (Command or Result).
 * @param j JSON object to parse.
 * @return Deserialized value, or error if type is unsupported or parsing fails.
 */
template <typename T>
std::expected<T, caudio::utils::Error> fromJson(const Json& j) {
    if constexpr (std::is_same_v<T, Command>) {
        return commandFromJson(j);
    } else if constexpr (std::is_same_v<T, Result>) {
        return resultFromJson(j);
    } else {
        return std::unexpected{caudio::utils::makeError(caudio::utils::StatusCode::Unsupported,
                                                        "unsupported fromJson type")};
    }
}

// ---------------------------------------------------------------------------
// Result JSON
// ---------------------------------------------------------------------------
/**
 * @brief Serialize a Result variant to JSON.
 * @ingroup caudio_ipc
 * @param r Result to serialize.
 * @return JSON object with "type" field and result-specific fields.
 */
Json toJson(const Result& r);

// ---------------------------------------------------------------------------
// IpcRequest / IpcReply serialization
// ---------------------------------------------------------------------------
/**
 * @brief Serialize an IpcRequest to JSON string.
 * @ingroup caudio_ipc
 * @param req Request to serialize.
 * @return JSON string (compact format).
 */
std::string serializeRequest(const IpcRequest& req);

/**
 * @brief Deserialize an IpcRequest from JSON string.
 * @ingroup caudio_ipc
 * @param sv JSON string to parse.
 * @return IpcRequest, or error if parsing fails or required fields are missing.
 */
std::expected<IpcRequest, caudio::utils::Error> deserializeRequest(std::string_view sv);

/**
 * @brief Serialize an IpcReply to JSON string.
 * @ingroup caudio_ipc
 * @param rep Reply to serialize.
 * @return JSON string (compact format).
 */
std::string serializeReply(const IpcReply& rep);

/**
 * @brief Deserialize an IpcReply from JSON string.
 * @ingroup caudio_ipc
 * @param sv JSON string to parse.
 * @return IpcReply, or error if parsing fails or required fields are missing.
 */
std::expected<IpcReply, caudio::utils::Error> deserializeReply(std::string_view sv);

// ---------------------------------------------------------------------------
// Framing: [4-byte BE len][json]
// ---------------------------------------------------------------------------
/**
 * @brief Frame a JSON string with 4-byte big-endian length prefix.
 * @ingroup caudio_ipc
 * @param json JSON payload to frame.
 * @return Vector of bytes: [len_be][json...] where len_be is 4-byte big-endian length.
 */
std::vector<std::byte> frame(std::string_view json);

/**
 * @brief Extract JSON payload from framed buffer.
 * @ingroup caudio_ipc
 * @param buf Buffer containing [4-byte BE len][json...].
 * @return JSON string payload, or error if frame is incomplete or invalid.
 */
std::expected<std::string, caudio::utils::Error> deframe(std::span<const std::byte> buf);

/**
 * @brief Serialize a Result to pretty-printed JSON string.
 * @ingroup caudio_ipc
 * @param r Result to serialize.
 * @return Pretty-printed JSON string (2-space indentation).
 */
std::string toJsonString(const Result& r);

/**
 * @brief Serialize a Command to pretty-printed JSON string.
 * @ingroup caudio_ipc
 * @param c Command to serialize.
 * @return Pretty-printed JSON string (2-space indentation).
 */
std::string toJsonString(const Command& c);

} // namespace caudio::ipc
