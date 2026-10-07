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
 * `caudio/app/` headers instead. Shell sources may include this during the
 * Phase-0 extraction only, until their groups move over.
 */
namespace caudio::app::detail {

/**
 * @brief Detects color-capable interactive terminals honoring NO_COLOR.
 * @return True when ANSI color may be emitted.
 */
bool useColor();

/**
 * @brief Prints Added lines for a QueueTracks result, warning on ids
 * already seen (pre-existing queue members).
 * @return The newly added count.
 */
int printAdded(const caudio::ipc::Result& res, std::set<int64_t>& seen);

/**
 * @brief Prints the "N track(s) added" total line.
 */
void countLine(int added);

} // namespace caudio::app::detail
