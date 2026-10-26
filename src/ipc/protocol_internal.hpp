#pragma once

/**
 * @file protocol_internal.hpp
 * @brief Internal IPC JSON helpers -- not part of the public API.
 * @ingroup caudio_ipc
 * @details Declares the JSON serialization helpers for enums, playlists,
 * and errors used by `src/ipc/protocol.cpp`. Lives in `src/` (not installed)
 * so downstream consumers never see them.
 */

#include <caudio/ipc/command.hpp>
#include <caudio/ipc/result.hpp>
#include <caudio/utils/error.hpp>
#include <caudio/utils/json.hpp>
#include <expected>
#include <string>
#include <string_view>

namespace caudio::ipc::detail {

using Json = caudio::utils::Json;

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

/**
 * @brief Convert Playlist struct to JSON object.
 * @ingroup caudio_ipc
 * @param p Playlist to convert.
 * @return JSON object with all playlist fields.
 */
Json playlistToJson(const caudio::types::Playlist& p);

/**
 * @brief Convert JSON object to Playlist struct.
 * @ingroup caudio_ipc
 * @param j JSON object with playlist fields.
 * @return Playlist struct, or error if parsing fails.
 */
std::expected<caudio::types::Playlist, caudio::utils::Error> playlistFromJson(const Json& j);

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

} // namespace caudio::ipc::detail
