#pragma once

#include <string>
#include <string_view>
#include <vector>

/**
 * @file paths.hpp
 * @brief File/glob/path utilities shared by all frontends.
 * @ingroup caudio_app
 * @details Moved out of the CLI shell: glob expansion for queue-add style
 * inputs, comparable path keys (separator-insensitive, like the daemon
 * matching in dispatch), and numeric-token checks. Frontends expand user
 * tokens with these, then hand file lists to App methods.
 */
namespace caudio::app {

/**
 * @brief Checks for glob characters (`*` or `?`).
 * @ingroup caudio_app
 */
bool hasGlobChars(std::string_view s);

/**
 * @brief Expands one add token into files.
 * @ingroup caudio_app
 * @param token Glob, folder, or single file path.
 * @param recursive Descend into subfolders for folder tokens.
 * @param files Out: absolutized audio files (sorted per token).
 * @param unmatched Out: tokens resolving to nothing (caller diagnoses).
 * @details Glob = non-recursive filename match in the pattern's parent dir;
 * folders yield top-level (or recursive) audio files; anything else lands
 * in `unmatched`.
 */
void expandAddToken(const std::string& token, bool recursive, std::vector<std::string>& files,
                    std::vector<std::string>& unmatched);

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
