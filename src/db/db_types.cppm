module;
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @file db_types.cppm
 * @brief Core value types for the caudio database layer.
 * @ingroup caudio_db
 * @details Defines plain-data structs used across all db partitions:
 * Track, Playlist, Queue/QueueItem, HistoryEntry, Bookmark, Library,
 * DbStats, LibraryStatsDetailedData, TrackQuery and HistoryQuery.
 * All types are trivially copyable aggregates with default-initialised
 * members; no invariants beyond those documented per-field.
 */

export module caudio.db:types;

export namespace caudio::db {

/**
 * @brief Represents a single audio track row.
 * @ingroup caudio_db
 * @details Maps 1:1 to the `tracks` table. `fingerprint` is a 32-byte
 * BLAKE3 digest (see fingerprint.cppm); `deleted_at == 0` means not
 * soft-deleted. `library_id` defaults to 1 (the built-in default library).
 */
struct Track {
    int64_t id{};                                ///< Row id (PK, 0 = not yet persisted).
    std::array<uint8_t, 32> fingerprint{};       ///< BLAKE3-256 content fingerprint (UNIQUE).
    std::string path;                            ///< Absolute filesystem path (NOT NULL).
    int64_t size{};                              ///< File size in bytes.
    int64_t mtime{};                             ///< Last write time (filesystem epoch).
    double duration{};                           ///< Duration in seconds.
    uint32_t sample_rate{};                      ///< Sample rate in Hz.
    uint32_t channels{};                         ///< Channel count.
    int32_t bitrate{};                           ///< Bitrate in bps.
    std::string title;                           ///< Title tag.
    std::string artist;                          ///< Artist tag.
    std::string album;                           ///< Album tag.
    std::string album_artist;                    ///< Album-artist tag.
    std::string genre;                           ///< Genre tag.
    int32_t year{};                              ///< Year tag.
    int32_t track_num{};                         ///< Track number within disc.
    int32_t disc_num{};                          ///< Disc number.
    std::string cover_art_path;                  ///< Path to cover art file.
    int32_t rating{};                            ///< User rating (0..5).
    int64_t play_count{};                        ///< Number of plays.
    int64_t last_played{};                       ///< Timestamp of last play (unix seconds).
    int64_t date_added{};                        ///< Timestamp when inserted.
    int64_t last_scanned{};                      ///< Timestamp of last scan.
    bool dirty{false};                           ///< True if metadata needs re-scan.
    int64_t deleted_at{};                        ///< Soft-delete timestamp (0 = live).
    int64_t library_id{1};                       ///< Owning library id (FK -> libraries.id).
};

/**
 * @brief Playlist row.
 * @ingroup caudio_db
 * @details Maps to `playlists`. `type == 0` is a manual playlist,
 * non-zero values denote smart playlists whose `smart_query` holds
 * the filter expression.
 */
struct Playlist {
    int64_t id{};                ///< Row id (PK).
    std::string name;            ///< Display name (NOT NULL).
    int32_t type{};              ///< Playlist type (0 = manual).
    std::string smart_query;     ///< Smart-playlist filter (empty for manual).
    int64_t created{};           ///< Creation timestamp.
    int64_t modified{};          ///< Last modification timestamp.
    int64_t library_id{1};       ///< Owning library id.
};

/**
 * @brief Single entry in a playback queue.
 * @ingroup caudio_db
 * @details Maps to `queue`. The table enforces `UNIQUE(queue_id, position)`
 * as a safety net; the single-writer invariant is maintained by
 * `Database::dbMutex_` (see schema.cppm and queue.cppm).
 */
struct QueueItem {
    int64_t id{};            ///< Row id (PK).
    int64_t queue_id{1};     ///< Owning queue id (FK -> queues.id).
    int64_t track_id{};      ///< Referenced track id (FK -> tracks.id).
    int64_t position{};      ///< Zero-based position within the queue (UNIQUE per queue_id).
    int64_t added{};         ///< Timestamp when enqueued.
};

/**
 * @brief Queue container row.
 * @ingroup caudio_db
 * @details Maps to `queues`. Separate from `queue` (items) table.
 */
struct Queue {
    int64_t id{};                ///< Row id (PK).
    std::string name;            ///< Display name.
    int32_t repeat_mode{};       ///< Repeat mode (0 = off).
    int64_t library_id{1};       ///< Owning library id.
};

/**
 * @brief Playback history entry.
 * @ingroup caudio_db
 * @details Maps to `history`. Records a single play session.
 */
struct HistoryEntry {
    int64_t id{};                ///< Row id (PK).
    int64_t track_id{};          ///< Played track id.
    int64_t started_at{};        ///< Start timestamp.
    int64_t completed_at{};      ///< Completion timestamp (0 if not completed).
    int64_t position_ms{};       ///< Position reached in milliseconds.
    double completion_pct{};     ///< Completion percentage [0,1].
    int64_t queue_id{1};         ///< Queue context.
};

/**
 * @brief Bookmark / saved position.
 * @ingroup caudio_db
 * @details Maps to `bookmarks`.
 */
struct Bookmark {
    int64_t id{};                ///< Row id (PK).
    int64_t track_id{};          ///< Bookmarked track id.
    int64_t position_ms{};       ///< Saved position in milliseconds.
    std::string note;            ///< Optional user note.
    int64_t created{};           ///< Creation timestamp.
};

/**
 * @brief Library (scan root) row.
 * @ingroup caudio_db
 * @details Maps to `libraries`. Id 1 is the default library with empty path.
 */
struct Library {
    int64_t id{};                                    ///< Row id (PK).
    std::string path;                                ///< Root directory path (UNIQUE).
    std::string name;                                ///< Display name.
    int64_t date_added{};                            ///< Creation timestamp.
    int64_t last_scanned{};                          ///< Last scan timestamp.
    bool auto_scan{true};                            ///< Whether to auto-scan on startup.
    bool recursive{true};                            ///< Whether to scan recursively.
    std::string extensions{"mp3,flac,ogg,wav,m4a"};  ///< Comma-separated allowed extensions.
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
    std::size_t tracks{0};               ///< Total live tracks.
    std::size_t queues{0};               ///< Total queue items.
    std::size_t playlists{0};            ///< Total playlists.
    int64_t total_duration_ms{0};        ///< Sum of durations (ms).
    int64_t total_play_time_ms{0};       ///< Sum of play_count * duration (ms).
    std::vector<Track> most_played{};    ///< Top 10 most-played tracks ordered by play_count DESC.
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
    std::optional<int64_t> track_id{};   ///< Filter by track.
    std::optional<int64_t> queue_id{};   ///< Filter by queue.
    int limit{};                         ///< Max rows (0 = unlimited).
    int offset{};                        ///< Row offset.
};

} // namespace caudio::db
