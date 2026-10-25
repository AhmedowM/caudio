#pragma once

/**
 * @file history.hpp
 * @brief Playback history results for the caudio IPC protocol.
 * @ingroup caudio_ipc
 */

#include <caudio/types/record.hpp>
#include <vector>

namespace caudio::ipc {

// ---------------------------------------------------------------------------
// History Results
// ---------------------------------------------------------------------------

/**
 * @struct History
 * @brief List of playback history entries (returned by HistoryList).
 * @ingroup caudio_ipc
 *
 * @json_example {"type": "History", "entries": [...]}
 *
 * @param entries Vector of history entries.
 */
struct History final {
    /**
     * @brief Vector of history entries.
     */
    std::vector<types::HistoryEntry> entries{};
};

} // namespace caudio::ipc
