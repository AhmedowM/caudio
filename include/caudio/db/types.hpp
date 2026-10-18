#pragma once

/**
 * @file types.hpp
 * @brief Core value types for the caudio database layer.
 * @ingroup caudio_db
 * @details Defines the db-local plain-data structs (Queue, Library, DbStats,
 * LibraryStatsDetailedData, TrackQuery, HistoryQuery) and re-exports the shared
 * records Track, Playlist, QueueItem, HistoryEntry and Bookmark as aliases to
 * `caudio::types` (see types/record.hpp). All are trivially copyable
 * aggregates with default-initialised members; no invariants beyond those
 * documented per-field.
 */

#include <caudio/types/record.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace caudio::db {

// Shared records live in caudio::types; keep caudio::db:: spellings working.
using caudio::types::Bookmark;
using caudio::types::HistoryEntry;
using caudio::types::Playlist;
using caudio::types::QueueItem;
using caudio::types::Track;

/**
 * @brief Queue container row.
 * @ingroup caudio_db
 * @details Maps to `queues`. Separate from `queue` (items) table.
 */
struct Queue {
    int64_t id{};          ///< Row id (PK).
    std::string name;      ///< Display name.
    int32_t repeat_mode{}; ///< Repeat mode (0 = off).
    bool temp{};           ///< Ephemeral direct-play queue (purged on shutdown).
    int64_t library_id{1}; ///< Owning library id.
};

/**
 * @brief Library (scan root) row.
 * @ingroup caudio_db
 * @details Maps to `libraries`. Id 1 is the default library with empty path.
 */
struct Library {
    int64_t id{};                                   ///< Row id (PK).
    std::string path;                               ///< Root directory path (UNIQUE).
    std::string name;                               ///< Display name.
    int64_t date_added{};                           ///< Creation timestamp.
    int64_t last_scanned{};                         ///< Last scan timestamp.
    bool auto_scan{true};                           ///< Whether to auto-scan on startup.
    bool recursive{true};                           ///< Whether to scan recursively.
    std::string extensions{"mp3,flac,ogg,wav,m4a"}; ///< Comma-separated allowed extensions.
};

/**
 * @brief Aggregate database statistics.
 * @ingroup caudio_db
 * @details Returned by `Database::getStats()`. Counts exclude soft-deleted tracks.
 */
struct DbStats {
    int64_t num_tracks{};        ///< Number of live tracks.
    int64_t num_playlists{};     ///< Number of playlists.
    int64_t num_queue_items{};   ///< Number of queue items.
    int64_t num_history{};       ///< Number of history entries.
    int64_t num_bookmarks{};     ///< Number of bookmarks.
    int64_t num_libraries{};     ///< Number of libraries.
    int64_t total_duration_ms{}; ///< Sum of track durations in milliseconds.
};

/**
 * @brief Detailed per-library statistics.
 * @ingroup caudio_db
 * @details Returned by `Database::libraryStatsDetailed()`.
 */
struct LibraryStatsDetailedData {
    std::size_t tracks{0};            ///< Total live tracks.
    std::size_t queues{0};            ///< Total queue items.
    std::size_t playlists{0};         ///< Total playlists.
    int64_t total_duration_ms{0};     ///< Sum of durations (ms).
    int64_t total_play_time_ms{0};    ///< Sum of play_count * duration (ms).
    std::vector<Track> most_played{}; ///< Top 10 most-played tracks ordered by play_count DESC.
};

/**
 * @brief Filter for listing tracks.
 * @ingroup caudio_db
 * @details All fields are optional filters combined with AND. `search`
 * maps to a LIKE on title/artist/album/genre. `limit == 0` means no limit;
 * `offset` requires `LIMIT -1` when no limit is set.
 */
struct TrackQuery {
    std::string artist;                  ///< Exact artist filter (empty = no filter).
    std::string album;                   ///< Exact album filter.
    std::string genre;                   ///< Exact genre filter.
    std::optional<int32_t> year{};       ///< Exact year filter.
    std::optional<int64_t> library_id{}; ///< Exact library filter.
    std::optional<bool> dirty{};         ///< Dirty flag filter.
    int limit{};                         ///< Max rows (0 = unlimited).
    int offset{};                        ///< Row offset.
    std::string search;                  ///< Substring search across title/artist/album/genre.
};

/**
 * @brief Filter for listing history entries.
 * @ingroup caudio_db
 */
struct HistoryQuery {
    std::optional<int64_t> track_id{}; ///< Filter by track.
    std::optional<int64_t> queue_id{}; ///< Filter by queue.
    int limit{};                       ///< Max rows (0 = unlimited).
    int offset{};                      ///< Row offset.
};

} // namespace caudio::db
