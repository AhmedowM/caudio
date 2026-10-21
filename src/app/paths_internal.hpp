#pragma once

/**
 * @file paths_internal.hpp
 * @brief App-internal path helpers (NOT public API).
 * @ingroup caudio_app
 * @details Comparable path keys and numeric-token checks used inside the
 * app layer. The public `caudio/app/paths.hpp` exposes only the glob
 * helpers, so frontends cannot depend on these.
 *
 * Internal header: lives in `src/`, not installed.
 */

#include <string>

namespace caudio::app {

/**
 * @brief Comparable path key: absolute + normalized (+ lowercase on Windows).
 * @ingroup caudio_app
 * @details Queued rows stored as relative paths resolve against the working
 * directory, matching rows the tooling itself added.
 */
std::string pathKey(const std::string& p);

/**
 * @brief Checks for an all-digit token (library id / position selectors).
 * @ingroup caudio_app
 */
bool isNumeric(const std::string& s);

} // namespace caudio::app
