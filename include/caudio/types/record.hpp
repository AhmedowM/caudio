#pragma once

/**
 * @file record.hpp
 * @brief Plain-data record types for the dependency-free `caudio.types` layer.
 * @ingroup caudio_types
 * @details Value structs shared across the db, engine, ipc and app layers:
 * Track, Playlist, QueueItem, HistoryEntry and Bookmark. All are trivially
 * copyable aggregates with default-initialised members; no invariants beyond
 * those documented per-field. `caudio::db` keeps deprecated aliases for the old
 * spellings (see `legacy_aliases.hpp`) until v2.0.0.
 */

#include <array>
#include <cstdint>
#include <string>

namespace caudio::types {

/**
 * @brief Represents a single audio track row.
 * @ingroup caudio_types
 * @details Maps 1:1 to the `tracks` table. `fingerprint` is a 32-byte
 * BLAKE3 digest; `deleted_at == 0` means not soft-deleted. `library_id`
 * defaults to 1 (the built-in default library).
 */
struct Track {
    int64_t id{};                          ///< Row id (PK, 0 = not yet persisted).
    std::array<uint8_t, 32> fingerprint{}; ///< BLAKE3-256 content fingerprint (UNIQUE).
    std::string path;                      ///< Absolute filesystem path (NOT NULL).
    int64_t size{};                        ///< File size in bytes.
    int64_t mtime{};                       ///< Last write time (filesystem epoch).
    double duration{};                     ///< Duration in seconds.
    uint32_t sample_rate{};                ///< Sample rate in Hz.
    uint32_t channels{};                   ///< Channel count.
    int32_t bitrate{};                     ///< Bitrate in bps.
    std::string title;                     ///< Title tag.
    std::string artist;                    ///< Artist tag.
    std::string album;                     ///< Album tag.
    std::string album_artist;              ///< Album-artist tag.
    std::string genre;                     ///< Genre tag.
    int32_t year{};                        ///< Year tag.
    int32_t track_num{};                   ///< Track number within disc.
    int32_t disc_num{};                    ///< Disc number.
    std::string cover_art_path;            ///< Path to cover art file.
    int32_t rating{};                      ///< User rating (0..5).
    int64_t play_count{};                  ///< Number of plays.
    int64_t last_played{};                 ///< Timestamp of last play (unix seconds).
    int64_t date_added{};                  ///< Timestamp when inserted.
    int64_t last_scanned{};                ///< Timestamp of last scan.
    bool dirty{false};                     ///< True if metadata needs re-scan.
    int64_t deleted_at{};                  ///< Soft-delete timestamp (0 = live).
    int64_t library_id{1};                 ///< Owning library id (FK -> libraries.id).
};

/**
 * @brief Playlist row.
 * @ingroup caudio_types
 * @details Maps to `playlists`. `type == 0` is a manual playlist,
 * non-zero values denote smart playlists whose `smart_query` holds
 * the filter expression.
 */
struct Playlist {
    int64_t id{};            ///< Row id (PK).
    std::string name;        ///< Display name (NOT NULL).
    int32_t type{};          ///< Playlist type (0 = manual).
    std::string smart_query; ///< Smart-playlist filter (empty for manual).
    int64_t created{};       ///< Creation timestamp.
    int64_t modified{};      ///< Last modification timestamp.
    int64_t library_id{1};   ///< Owning library id.
};

/**
 * @brief Single entry in a playback queue.
 * @ingroup caudio_types
 * @details Maps to `queue`. The table enforces `UNIQUE(queue_id, position)`
 * as a safety net; the single-writer invariant is maintained by
 * `Database::dbMutex_`.
 */
struct QueueItem {
    int64_t id{};        ///< Row id (PK).
    int64_t queue_id{1}; ///< Owning queue id (FK -> queues.id).
    int64_t track_id{};  ///< Referenced track id (FK -> tracks.id).
    int64_t position{};  ///< Zero-based position within the queue (UNIQUE per queue_id).
    int64_t added{};     ///< Timestamp when enqueued.
};

/**
 * @brief Playback history entry.
 * @ingroup caudio_types
 * @details First 7 fields map 1:1 to the `history` table row. The trailing
 * `title`/`artist`/`path`/`duration` are track snapshots populated only by
 * JOIN queries (`history` + `tracks`); they are empty on freshly marked
 * rows and ignored on insert. Single definition shared by db, engine
 * and service layers (the former `engine::HistoryEntry` duplicate is deleted).
 */
struct HistoryEntry {
    int64_t id{};            ///< Row id (PK).
    int64_t track_id{};      ///< Played track id.
    int64_t started_at{};    ///< Start timestamp.
    int64_t completed_at{};  ///< Completion timestamp (0 if not completed).
    int64_t position_ms{};   ///< Position reached in milliseconds.
    double completion_pct{}; ///< Completion percentage [0,1].
    int64_t queue_id{1};     ///< Queue context.
    std::string title{};     ///< Track title snapshot (JOIN queries only).
    std::string artist{};    ///< Track artist snapshot (JOIN queries only).
    std::string path{};      ///< Track path snapshot (JOIN queries only).
    double duration{};       ///< Track duration snapshot (JOIN queries only).
};

/**
 * @brief Bookmark / saved position.
 * @ingroup caudio_types
 * @details Maps to `bookmarks`.
 */
struct Bookmark {
    int64_t id{};          ///< Row id (PK).
    int64_t track_id{};    ///< Bookmarked track id.
    int64_t position_ms{}; ///< Saved position in milliseconds.
    std::string note;      ///< Optional user note.
    int64_t created{};     ///< Creation timestamp.
};

} // namespace caudio::types
