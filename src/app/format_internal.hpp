#pragma once

/**
 * @file format_internal.hpp
 * @brief App-internal display formatting helpers (NOT public API).
 * @ingroup caudio_app
 * @details Track labels and the playlist writers used inside the app layer.
 * The public `caudio/app/format.hpp` exposes only the clock formatter, so
 * frontends cannot depend on these helpers.
 *
 * Internal header: lives in `src/`, not installed.
 */

#include <caudio/ipc/result/playback.hpp>
#include <caudio/types/record.hpp>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace caudio::app {

/**
 * @brief Human track label for transport confirmations.
 * @ingroup caudio_app
 * @param st Status carrying artist/title/path.
 * @return "artist - title", either half, the filename, or "unknown track".
 */
std::string trackWho(const caudio::ipc::Status& st);

/**
 * @brief Human label for added tracks: "artist - title: file.ext".
 * @ingroup caudio_app
 * @details Artist/title parts omitted when empty, bare filename when both
 * are, "track <id>" when nothing else exists.
 */
std::string addedLabel(const caudio::types::Track& t);

/**
 * @brief Writes tracks as M3U/PLS/plain text.
 * @ingroup caudio_app
 * @param format "m3u", "pls", or anything else for plain paths.
 */
void writePlaylistText(std::ostream& os, const std::vector<caudio::types::Track>& tracks,
                       std::string_view format);

/**
 * @brief Writes tracks as `{"format":"caudio-playlist",...}` JSON.
 * @ingroup caudio_app
 */
void writePlaylistJson(std::ostream& os, const std::vector<caudio::types::Track>& tracks);

} // namespace caudio::app
