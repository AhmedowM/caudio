#pragma once
/**
 * @file protocol.hpp
 * @brief IPC protocol implementation for caudio CLI communication.
 * @ingroup caudio_cli
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
 * @see caudio::cli::Command for command types
 * @see caudio::cli::Result for result types
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "caudio/db/db_types.hpp"
#include "caudio/engine/engine.hpp"
#include "caudio/json/json.hpp"
#include "caudio/utils/utils.hpp"
#include "cli/shared/command.hpp"
#include "cli/shared/result.hpp"

namespace caudio::cli {

using ordered_json = caudio::json::ordered_json;

/**
 * @struct IpcRequest
 * @brief IPC request message sent from client to daemon.
 * @ingroup caudio_cli
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
 * @ingroup caudio_cli
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
 * @ingroup caudio_cli
 * @param s Playback state to convert.
 * @return String representation ("Stopped", "Ready", "Playing", "Paused", "Unknown").
 */
std::string playbackStateToString(caudio::engine::PlaybackState s);

/**
 * @brief Convert string to PlaybackState enum.
 * @ingroup caudio_cli
 * @param sv String to parse ("Stopped", "Ready", "Playing", "Paused").
 * @return PlaybackState enum value, or error if unknown.
 */
std::expected<caudio::engine::PlaybackState, caudio::utils::Error>
playbackStateFromString(std::string_view sv);

/**
 * @brief Convert RepeatMode enum to string representation.
 * @ingroup caudio_cli
 * @param m Repeat mode to convert.
 * @return String representation ("Off", "Queue", "One", "Unknown").
 */
std::string repeatModeToString(caudio::engine::RepeatMode m);

/**
 * @brief Convert string to RepeatMode enum.
 * @ingroup caudio_cli
 * @param sv String to parse ("Off", "Queue", "One").
 * @return RepeatMode enum value, or error if unknown.
 */
std::expected<caudio::engine::RepeatMode, caudio::utils::Error>
repeatModeFromString(std::string_view sv);

/**
 * @brief Convert StatusCode enum to string representation.
 * @ingroup caudio_cli
 * @param r Status code to convert.
 * @return String representation (e.g., "Ok", "InvalidArg", "NotFound").
 */
std::string resultCodeToString(caudio::utils::StatusCode r);

/**
 * @brief Convert string to StatusCode enum.
 * @ingroup caudio_cli
 * @param sv String to parse.
 * @return StatusCode enum value, or error if unknown.
 */
std::expected<caudio::utils::StatusCode, caudio::utils::Error>
resultCodeFromString(std::string_view sv);

// Track JSON helpers
/**
 * @brief Convert Track struct to JSON object.
 * @ingroup caudio_cli
 * @param t Track to convert.
 * @return JSON object with all track fields.
 */
ordered_json trackToJson(const caudio::db::Track& t);

/**
 * @brief Convert JSON object to Track struct.
 * @ingroup caudio_cli
 * @param j JSON object with track fields.
 * @return Track struct, or error if parsing fails.
 */
std::expected<caudio::db::Track, caudio::utils::Error> trackFromJson(const ordered_json& j);

/**
 * @brief Convert Playlist struct to JSON object.
 * @ingroup caudio_cli
 * @param p Playlist to convert.
 * @return JSON object with all playlist fields.
 */
ordered_json playlistToJson(const caudio::db::Playlist& p);

/**
 * @brief Convert JSON object to Playlist struct.
 * @ingroup caudio_cli
 * @param j JSON object with playlist fields.
 * @return Playlist struct, or error if parsing fails.
 */
std::expected<caudio::db::Playlist, caudio::utils::Error> playlistFromJson(const ordered_json& j);

/**
 * @brief Convert Error struct to JSON object.
 * @ingroup caudio_cli
 * @param e Error to convert.
 * @return JSON object with type, code, code_value, and message fields.
 */
ordered_json errorToJson(const caudio::utils::Error& e);

/**
 * @brief Convert JSON object to Error struct.
 * @ingroup caudio_cli
 * @param j JSON object with code/code_value and message fields.
 * @return Error struct, or error if parsing fails.
 */
std::expected<caudio::utils::Error, caudio::utils::Error> errorFromJson(const ordered_json& j);

} // namespace detail

// ---------------------------------------------------------------------------
// Command JSON
// ---------------------------------------------------------------------------
/**
 * @brief Serialize a Command variant to JSON.
 * @ingroup caudio_cli
 * @param cmd Command to serialize.
 * @return JSON object with "type" field and command-specific fields.
 */
ordered_json toJson(const Command& cmd);

/**
 * @brief Deserialize a Command from JSON.
 * @ingroup caudio_cli
 * @param j JSON object with "type" field and command-specific fields.
 * @return Command variant, or error if type is unknown or parsing fails.
 */
std::expected<Command, caudio::utils::Error> commandFromJson(const ordered_json& j);

// Generic fromJson template wrapper: fromJson<Command>(json)
/**
 * @brief Generic template for deserializing Command or Result from JSON.
 * @ingroup caudio_cli
 * @tparam T Type to deserialize (Command or Result).
 * @param j JSON object to parse.
 * @return Deserialized value, or error if type is unsupported or parsing fails.
 * @note For Result type, use resultFromJson directly.
 */
template <typename T>
std::expected<T, caudio::utils::Error> fromJson(const ordered_json& j) {
    if constexpr (std::is_same_v<T, Command>) {
        return commandFromJson(j);
    } else if constexpr (std::is_same_v<T, Result>) {
        // forwarded to resultFromJson declared below; use if constexpr dispatch via overload
        // This branch will be instantiated only for Result; to avoid incomplete type,
        // we handle Result via separate function resultFromJson and call it here.
        // We need forward declaration: implement after Result helpers.
        return std::unexpected{
            caudio::utils::makeError(caudio::utils::StatusCode::Unsupported, "use resultFromJson")};
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
 * @ingroup caudio_cli
 * @param r Result to serialize.
 * @return JSON object with "type" field and result-specific fields.
 */
ordered_json toJson(const Result& r);

/**
 * @brief Deserialize a Result from JSON.
 * @ingroup caudio_cli
 * @param j JSON object with "type" field and result-specific fields.
 * @return Result variant, or error if type is unknown or parsing fails.
 */
std::expected<Result, caudio::utils::Error> resultFromJson(const ordered_json& j);

// ---------------------------------------------------------------------------
// IpcRequest / IpcReply serialization
// ---------------------------------------------------------------------------
/**
 * @brief Serialize an IpcRequest to JSON string.
 * @ingroup caudio_cli
 * @param req Request to serialize.
 * @return JSON string (compact format).
 */
std::string serializeRequest(const IpcRequest& req);

/**
 * @brief Deserialize an IpcRequest from JSON string.
 * @ingroup caudio_cli
 * @param sv JSON string to parse.
 * @return IpcRequest, or error if parsing fails or required fields are missing.
 */
std::expected<IpcRequest, caudio::utils::Error> deserializeRequest(std::string_view sv);

/**
 * @brief Serialize an IpcReply to JSON string.
 * @ingroup caudio_cli
 * @param rep Reply to serialize.
 * @return JSON string (compact format).
 */
std::string serializeReply(const IpcReply& rep);

/**
 * @brief Deserialize an IpcReply from JSON string.
 * @ingroup caudio_cli
 * @param sv JSON string to parse.
 * @return IpcReply, or error if parsing fails or required fields are missing.
 */
std::expected<IpcReply, caudio::utils::Error> deserializeReply(std::string_view sv);

// ---------------------------------------------------------------------------
// Framing: [4-byte BE len][json]
// ---------------------------------------------------------------------------
/**
 * @brief Frame a JSON string with 4-byte big-endian length prefix.
 * @ingroup caudio_cli
 * @param json JSON payload to frame.
 * @return Vector of bytes: [len_be][json...] where len_be is 4-byte big-endian length.
 */
std::vector<std::byte> frame(std::string_view json);

/**
 * @brief Extract JSON payload from framed buffer.
 * @ingroup caudio_cli
 * @param buf Buffer containing [4-byte BE len][json...].
 * @return JSON string payload, or error if frame is incomplete or invalid.
 */
std::expected<std::string, caudio::utils::Error> deframe(std::span<const std::byte> buf);

/**
 * @brief Serialize a Result to pretty-printed JSON string.
 * @ingroup caudio_cli
 * @param r Result to serialize.
 * @return Pretty-printed JSON string (2-space indentation).
 */
std::string toJsonString(const Result& r);

/**
 * @brief Serialize a Command to pretty-printed JSON string.
 * @ingroup caudio_cli
 * @param c Command to serialize.
 * @return Pretty-printed JSON string (2-space indentation).
 */
std::string toJsonString(const Command& c);

} // namespace caudio::cli
