#pragma once

/**
 * @file format.hpp
 * @brief Display formatting shared by all frontends.
 * @ingroup caudio_app
 * @details Tiny pure helper for rendering playback state: clock strings
 * (no ids, no queue positions -- see listings for those). Moved out of the
 * CLI shell so TUIs render identically. Track labels and the playlist
 * writers are app-internal (`src/app/format_internal.hpp`).
 */

#include <string>

namespace caudio::app {

/**
 * @brief Formats seconds as mm:ss or hh:mm:ss.
 * @ingroup caudio_app
 * @param secs Position or duration in seconds (negative clamps to 0).
 */
std::string fmtClock(double secs);

} // namespace caudio::app
