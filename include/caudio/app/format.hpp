#pragma once

/**
 * @file format.hpp
 * @brief Display formatting shared by all frontends.
 * @ingroup caudio_app
 * @details Tiny pure helpers for rendering playback state: clock strings
 * and human track labels (no ids, no queue positions -- see listings for
 * those). Moved out of the CLI shell so TUIs render identically.
 */

#include <caudio/db/types.hpp>
#include <caudio/ipc/result.hpp>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace caudio::app {

/**
 * @brief Formats seconds as mm:ss or hh:mm:ss.
 * @ingroup caudio_app
 * @param secs Position or duration in seconds (negative clamps to 0).
 */
std::string fmtClock(double secs);

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
std::string addedLabel(const caudio::db::Track& t);

/**
 * @brief Writes tracks as M3U/PLS/plain text.
 * @ingroup caudio_app
 * @param format "m3u", "pls", or anything else for plain paths.
 */
void writePlaylistText(std::ostream& os, const std::vector<caudio::db::Track>& tracks,
                       std::string_view format);

/**
 * @brief Writes tracks as `{"format":"caudio-playlist",...}` JSON.
 * @ingroup caudio_app
 */
void writePlaylistJson(std::ostream& os, const std::vector<caudio::db::Track>& tracks);

} // namespace caudio::app
