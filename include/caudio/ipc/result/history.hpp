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
 * @brief Single playback history entry.
 * @ingroup caudio_ipc
 * @details Same-type alias of `caudio::types::HistoryEntry`; kept for IPC
 * source compatibility. JSON shape is unchanged.
 *
 * @json_example {"type": "HistoryEntry", "id": 1, "track_id": 123, "started_at": 1699999999000,
 * "completed_at": 1699999999240, "position_ms": 240000, "completion_pct": 100.0, "queue_id": 1,
 * "title": "Song", "artist": "Artist", "path": "/music/song.mp3", "duration": 240.0}
 *
 * @param id History entry ID.
 * @param track_id Track ID that was played.
 * @param started_at Playback start timestamp (milliseconds since epoch).
 * @param completed_at Playback completion timestamp (milliseconds since epoch).
 * @param position_ms Playback position at completion in milliseconds.
 * @param completion_pct Completion percentage (0.0 to 100.0).
 * @param queue_id Queue ID the track was played from.
 * @param title Track title (cached for display).
 * @param artist Track artist (cached for display).
 * @param path Track file path (cached for display).
 * @param duration Track duration in seconds.
 */
using HistoryEntry = caudio::types::HistoryEntry;

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
    std::vector<HistoryEntry> entries{};
};

} // namespace caudio::ipc
