#pragma once

#include <caudio/ipc/result.hpp>
#include <cstdint>
#include <set>
#include <string>

/**
 * @file detail.hpp
 * @brief App-internal reporting helpers (NOT public API).
 * @details Queue-add style "Added" reporting and terminal color detection,
 * shared between app command handlers. Frontends must use the public
 * `caudio/app/` headers instead. Phase 1: builders return text instead of
 * printing; useColor moves to frontends as they convert.
 */
namespace caudio::app::detail {

/**
 * @brief Detects color-capable interactive terminals honoring NO_COLOR.
 * @return True when ANSI color may be emitted.
 * @details Transitional: frontends take their own copy as they convert
 * (the shell already has one); deleted once no lib user remains.
 */
bool useColor();

/**
 * @brief Prints Added lines for a QueueTracks result, warning on ids
 * already seen (pre-existing queue members).
 * @return The newly added count.
 * @details Transitional: queue.cpp moves to printAddedText; deleted after.
 */
int printAdded(const caudio::ipc::Result& res, std::set<int64_t>& seen);

/**
 * @brief Prints the "N track(s) added" total line.
 * @details Transitional: queue.cpp moves to countLineText; deleted after.
 */
void countLine(int added);

/**
 * @brief Appends one line to a blob (newline-joined, no trailing newline).
 */
void emitLine(std::string& blob, const std::string& line);

/**
 * @brief Appends formatter-style block text, preserving interior blanks.
 */
void appendBlock(std::string& blob, const std::string& text);

/**
 * @brief Renders a result with the human formatter into a blob.
 */
void renderInto(std::string& blob, const caudio::ipc::Result& res);

/**
 * @brief Builds Added lines for a QueueTracks result, warning on ids
 * already seen (pre-existing queue members).
 * @return The newly added count.
 */
int printAddedText(const caudio::ipc::Result& res, std::set<int64_t>& seen, std::string& out,
                   std::string& err);

/**
 * @brief Builds the "N track(s) added" total line.
 */
void countLineText(int added, std::string& out);

} // namespace caudio::app::detail
