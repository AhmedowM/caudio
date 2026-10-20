#pragma once

/**
 * @file library.hpp
 * @brief Library statistics and track results for the caudio IPC protocol.
 * @ingroup caudio_ipc
 */

#include <caudio/types/record.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace caudio::ipc {

// ---------------------------------------------------------------------------
// Library Statistics Results
// ---------------------------------------------------------------------------

// Note: Command already defines empty LibraryStats request. Result's stats
// is named LibraryStatsData to avoid ODR collision when both partitions are
// imported via caudio.ipc:shared. It aliases the canonical caudio::types type.

/**
 * @brief Basic library statistics (returned by LibraryStats and QueueQueues).
 * @ingroup caudio_ipc
 * @details Same-type alias of `caudio::types::LibraryStats`; kept for IPC
 * source compatibility. JSON shape is unchanged.
 *
 * @json_example {"type": "LibraryStats", "tracks": 1000, "queues": 5, "playlists": 20}
 *
 * @param tracks Total number of tracks in the library.
 * @param queues Total number of queue items.
 * @param playlists Total number of playlists.
 */
using LibraryStatsData = caudio::types::LibraryStats;

/**
 * @brief Detailed library statistics including play history (returned by LibraryStatsDetailed).
 * @ingroup caudio_ipc
 * @details Same-type alias of `caudio::types::LibraryStatsDetailed`; kept for
 * IPC source compatibility. JSON shape is unchanged.
 *
 * @json_example {"type": "LibraryStatsDetailed", "tracks": 1000, "queues": 5, "playlists": 20,
 * "total_duration_ms": 36000000, "total_play_time_ms": 7200000, "most_played": [...]}
 *
 * @param tracks Total number of tracks in the library.
 * @param queues Total number of queues.
 * @param playlists Total number of playlists.
 * @param total_duration_ms Total duration of all tracks in milliseconds.
 * @param total_play_time_ms Total play time across all tracks in milliseconds.
 * @param most_played Most played tracks (up to 10).
 */
using LibraryStatsDetailedData = caudio::types::LibraryStatsDetailed;

// ---------------------------------------------------------------------------
// Track and Playlist Results
// ---------------------------------------------------------------------------

/**
 * @struct Tracks
 * @brief List of tracks (returned by LibraryList).
 * @ingroup caudio_ipc
 *
 * @json_example {"type": "Tracks", "tracks": [{"id": 1, "title": "Song", ...}]}
 *
 * @param tracks Vector of tracks.
 */
struct Tracks final {
    /**
     * @brief Vector of tracks.
     */
    std::vector<caudio::types::Track> tracks{};
};

/**
 * @struct SearchResults
 * @brief Library search hits with the query (returned by LibrarySearch).
 * @ingroup caudio_ipc
 *
 * @json_example {"type": "SearchResults", "query": "beatles", "tracks": [...]}
 *
 * @param query Search query string (for match highlighting).
 * @param tracks Matching tracks.
 */
struct SearchResults final {
    /**
     * @brief Search query string.
     */
    std::string query{};
    /**
     * @brief Matching tracks.
     */
    std::vector<caudio::types::Track> tracks{};
};

/**
 * @struct ScanReport
 * @brief Library scan outcome (returned by LibraryScan).
 * @ingroup caudio_ipc
 *
 * @json_example {"type": "ScanReport", "added": 12}
 *
 * @param added Number of newly inserted tracks.
 */
struct ScanReport final {
    /**
     * @brief Number of newly inserted tracks.
     */
    std::size_t added{0};
};

// ---------------------------------------------------------------------------
// Track Detail Results
// ---------------------------------------------------------------------------

/**
 * @struct SingleTrack
 * @brief Single track with full metadata (returned by TagGet, LibrarySearch single result).
 * @ingroup caudio_ipc
 *
 * @json_example {"type": "SingleTrack", "track": {"id": 1, "title": "Song", ...}}
 *
 * @param track Track metadata.
 */
struct SingleTrack final {
    /**
     * @brief Track metadata.
     */
    caudio::types::Track track{};
};

/**
 * @struct TrackInfo
 * @brief Track with play statistics (returned by Info command).
 * @ingroup caudio_ipc
 *
 * @json_example {"type": "TrackInfo", "track": {...}, "play_count": 42, "last_played":
 * 1699999999000}
 *
 * @param track Track metadata.
 * @param play_count Number of times this track has been played.
 * @param last_played Timestamp of last play (milliseconds since epoch, 0 if never).
 */
struct TrackInfo final {
    /**
     * @brief Track metadata.
     */
    caudio::types::Track track{};
    /**
     * @brief Number of times this track has been played.
     */
    int64_t play_count{0};
    /**
     * @brief Timestamp of last play (milliseconds since epoch, 0 if never).
     */
    int64_t last_played{0};
};

} // namespace caudio::ipc
