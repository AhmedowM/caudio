#pragma once

#include <caudio/db/db_types.hpp>
#include <caudio/ipc/result.hpp>
#include <string>

/**
 * @file format.hpp
 * @brief Display formatting shared by all frontends.
 * @ingroup caudio_app
 * @details Tiny pure helpers for rendering playback state: clock strings
 * and human track labels (no ids, no queue positions -- see listings for
 * those). Moved out of the CLI shell so TUIs render identically.
 */
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

} // namespace caudio::app
