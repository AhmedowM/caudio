#ifndef CAUDIO_TYPES_RECORD_H
#define CAUDIO_TYPES_RECORD_H

/**
 * @file caudio/types/record.h
 * @brief Plain-data domain records shared across the database, engine, IPC and
 *        app layers.
 *
 * Every record mirrors the corresponding `kanariv::types` value type field for
 * field. Records with owning members expose `_init`, `_copy`, `_move` and
 * `_destroy`; POD records expose the same functions for uniformity.
 *
 * Ownership rules:
 * - `_init` initializes a record in uninitialized storage. On failure the
 *   record is left zeroed and must not be destroyed.
 * - `_copy` initializes @p dst from @p src using @p alloc (nullptr = default).
 * - `_move` destroys @p dst's current contents and steals @p src's; @p src is
 *   left zeroed.
 * - `_destroy` releases all owned memory and zeros the record. It is safe to
 *   call on a zeroed record.
 *
 * String members are `cx_string *` handles. Vector members own their pointees
 * unless documented otherwise; use the matching record lifecycle function for
 * each element.
 */

#include <stddef.h>
#include <stdint.h>

#include <caudio/api.h>
#include <caudio/runtime/status.h>
#include <caudio/types/wire.h>

#include <cstdx/allocator.h>
#include <cstdx/string.h>
#include <cstdx/vector.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Source identity                                                           */
/* ------------------------------------------------------------------------- */

/**
 * @brief Persisted source identity and availability for a track.
 *
 * `key` is the stable identity used for deduplication by `(kind, key)`.
 */
typedef struct ca_source_ref {
    ca_source_kind kind;   /**< Durable identity category. */
    cx_string *key;        /**< Stable identity; empty only before normalization. */
    cx_string *display_name; /**< Optional user-facing name. */
    bool available;        /**< False when the host cannot reopen the source. */
} ca_source_ref;

CAUDIO_API ca_status ca_source_ref_init(ca_source_ref *ref,
                                        const cx_allocator *alloc);
CAUDIO_API ca_status ca_source_ref_copy(ca_source_ref *dst,
                                        const ca_source_ref *src,
                                        const cx_allocator *alloc);
CAUDIO_API void ca_source_ref_move(ca_source_ref *dst, ca_source_ref *src);
CAUDIO_API void ca_source_ref_destroy(ca_source_ref *ref);

/* ------------------------------------------------------------------------- */
/* Records                                                                   */
/* ------------------------------------------------------------------------- */

/**
 * @brief A single audio track row.
 *
 * Maps 1:1 to the `tracks` table. `fingerprint` is a 32-byte BLAKE3 digest;
 * `deleted_at == 0` means not soft-deleted; `library_id` defaults to 1.
 */
typedef struct ca_track {
    int64_t id;
    int64_t size;
    int64_t mtime;
    double duration;
    int64_t play_count;
    int64_t last_played;
    int64_t date_added;
    int64_t last_scanned;
    int64_t deleted_at;
    int64_t library_id;
    cx_string *path;
    cx_string *title;
    cx_string *artist;
    cx_string *album;
    cx_string *album_artist;
    cx_string *genre;
    cx_string *cover_art_path;
    uint32_t sample_rate;
    uint32_t channels;
    int32_t bitrate;
    int32_t year;
    int32_t track_num;
    int32_t disc_num;
    int32_t rating;
    uint8_t fingerprint[32];
    bool dirty;
    ca_source_ref source;
} ca_track;

CAUDIO_API ca_status ca_track_init(ca_track *track,
                                   const cx_allocator *alloc);
CAUDIO_API ca_status ca_track_copy(ca_track *dst, const ca_track *src,
                                   const cx_allocator *alloc);
CAUDIO_API void ca_track_move(ca_track *dst, ca_track *src);
CAUDIO_API void ca_track_destroy(ca_track *track);

/**
 * @brief Return a persistence-ready source reference for a track.
 *
 * Mirrors `kanariv::types::normalizedSource`: for `CA_SOURCE_PATH`, an empty
 * key and display name are filled from `track->path`. Other kinds are copied
 * unchanged.
 */
CAUDIO_API ca_status ca_track_normalized_source(
    const ca_track *track, ca_source_ref *out, const cx_allocator *alloc);

/**
 * @brief Playlist row.
 *
 * `type == 0` is a manual playlist; non-zero values denote smart playlists
 * whose `smart_query` holds the filter expression.
 */
typedef struct ca_playlist {
    int64_t id;
    cx_string *name;
    int32_t type;
    cx_string *smart_query;
    int64_t created;
    int64_t modified;
    int64_t library_id;
} ca_playlist;

CAUDIO_API ca_status ca_playlist_init(ca_playlist *playlist,
                                      const cx_allocator *alloc);
CAUDIO_API ca_status ca_playlist_copy(ca_playlist *dst,
                                      const ca_playlist *src,
                                      const cx_allocator *alloc);
CAUDIO_API void ca_playlist_move(ca_playlist *dst, ca_playlist *src);
CAUDIO_API void ca_playlist_destroy(ca_playlist *playlist);

/**
 * @brief Single entry in a playback queue.
 *
 * Maps to the `queue` table. `(queue_id, position)` is unique.
 */
typedef struct ca_queue_item {
    int64_t id;
    int64_t queue_id;
    int64_t track_id;
    int64_t position;
    int64_t added;
} ca_queue_item;

CAUDIO_API ca_status ca_queue_item_init(ca_queue_item *item,
                                        const cx_allocator *alloc);
CAUDIO_API ca_status ca_queue_item_copy(ca_queue_item *dst,
                                        const ca_queue_item *src,
                                        const cx_allocator *alloc);
CAUDIO_API void ca_queue_item_move(ca_queue_item *dst, ca_queue_item *src);
CAUDIO_API void ca_queue_item_destroy(ca_queue_item *item);

/**
 * @brief Playback history entry.
 *
 * The first seven fields map to the `history` table row; the trailing
 * `title`/`artist`/`path`/`duration` are track snapshots populated only by
 * JOIN queries.
 */
typedef struct ca_history_entry {
    int64_t id;
    int64_t track_id;
    int64_t started_at;
    int64_t completed_at;
    int64_t position_ms;
    double completion_pct;
    int64_t queue_id;
    cx_string *title;
    cx_string *artist;
    cx_string *path;
    double duration;
} ca_history_entry;

CAUDIO_API ca_status ca_history_entry_init(ca_history_entry *entry,
                                           const cx_allocator *alloc);
CAUDIO_API ca_status ca_history_entry_copy(ca_history_entry *dst,
                                           const ca_history_entry *src,
                                           const cx_allocator *alloc);
CAUDIO_API void ca_history_entry_move(ca_history_entry *dst,
                                      ca_history_entry *src);
CAUDIO_API void ca_history_entry_destroy(ca_history_entry *entry);

/**
 * @brief Bookmark / saved position.
 */
typedef struct ca_bookmark {
    int64_t id;
    int64_t track_id;
    int64_t position_ms;
    cx_string *note;
    int64_t created;
} ca_bookmark;

CAUDIO_API ca_status ca_bookmark_init(ca_bookmark *bookmark,
                                      const cx_allocator *alloc);
CAUDIO_API ca_status ca_bookmark_copy(ca_bookmark *dst,
                                      const ca_bookmark *src,
                                      const cx_allocator *alloc);
CAUDIO_API void ca_bookmark_move(ca_bookmark *dst, ca_bookmark *src);
CAUDIO_API void ca_bookmark_destroy(ca_bookmark *bookmark);

/**
 * @brief Audio playback device information.
 */
typedef struct ca_device_info {
    cx_string *id;
    cx_string *name;
    bool is_default;
} ca_device_info;

CAUDIO_API ca_status ca_device_info_init(ca_device_info *device,
                                         const cx_allocator *alloc);
CAUDIO_API ca_status ca_device_info_copy(ca_device_info *dst,
                                         const ca_device_info *src,
                                         const cx_allocator *alloc);
CAUDIO_API void ca_device_info_move(ca_device_info *dst,
                                    ca_device_info *src);
CAUDIO_API void ca_device_info_destroy(ca_device_info *device);

/**
 * @brief Container for enumerated audio devices.
 *
 * `devices` holds `ca_device_info *` elements. The list owns each element.
 */
typedef struct ca_device_list {
    cx_vector *devices;
} ca_device_list;

CAUDIO_API ca_status ca_device_list_init(ca_device_list *list,
                                         const cx_allocator *alloc);
CAUDIO_API ca_status ca_device_list_copy(ca_device_list *dst,
                                         const ca_device_list *src,
                                         const cx_allocator *alloc);
CAUDIO_API void ca_device_list_move(ca_device_list *dst,
                                    ca_device_list *src);
CAUDIO_API void ca_device_list_destroy(ca_device_list *list);

/**
 * @brief Audio file metadata tags.
 */
typedef struct ca_track_metadata {
    cx_string *title;
    cx_string *artist;
    cx_string *album;
    cx_string *album_artist;
    cx_string *genre;
    int year;
    int track_num;
    int disc_num;
    double duration;
    int sample_rate;
    int channels;
    int bitrate;
} ca_track_metadata;

CAUDIO_API ca_status ca_track_metadata_init(ca_track_metadata *metadata,
                                            const cx_allocator *alloc);
CAUDIO_API ca_status ca_track_metadata_copy(ca_track_metadata *dst,
                                            const ca_track_metadata *src,
                                            const cx_allocator *alloc);
CAUDIO_API void ca_track_metadata_move(ca_track_metadata *dst,
                                       ca_track_metadata *src);
CAUDIO_API void ca_track_metadata_destroy(ca_track_metadata *metadata);

/**
 * @brief Top entity entry for listening reports (artist/album/genre).
 */
typedef struct ca_top_entity {
    cx_string *name;
    size_t track_count;
    int64_t total_plays;
} ca_top_entity;

CAUDIO_API ca_status ca_top_entity_init(ca_top_entity *entity,
                                        const cx_allocator *alloc);
CAUDIO_API ca_status ca_top_entity_copy(ca_top_entity *dst,
                                        const ca_top_entity *src,
                                        const cx_allocator *alloc);
CAUDIO_API void ca_top_entity_move(ca_top_entity *dst, ca_top_entity *src);
CAUDIO_API void ca_top_entity_destroy(ca_top_entity *entity);

/**
 * @brief Library statistics with always-computed totals and optional top-N
 *        lists.
 *
 * `top_tracks` holds `ca_track *` elements; the other top lists hold
 * `ca_top_entity *` elements. A list is present only when its `has_*` flag is
 * true; a present empty list means "totals only".
 */
typedef struct ca_library_stats {
    size_t tracks;
    size_t queues;
    size_t playlists;
    int64_t total_duration_ms;
    int64_t total_play_time_ms;
    bool has_top_tracks;
    cx_vector *top_tracks;
    bool has_top_albums;
    cx_vector *top_albums;
    bool has_top_genres;
    cx_vector *top_genres;
    bool has_top_artists;
    cx_vector *top_artists;
} ca_library_stats;

CAUDIO_API ca_status ca_library_stats_init(ca_library_stats *stats,
                                           const cx_allocator *alloc);
CAUDIO_API ca_status ca_library_stats_copy(ca_library_stats *dst,
                                           const ca_library_stats *src,
                                           const cx_allocator *alloc);
CAUDIO_API void ca_library_stats_move(ca_library_stats *dst,
                                      ca_library_stats *src);
CAUDIO_API void ca_library_stats_destroy(ca_library_stats *stats);

/**
 * @brief Queue container row.
 *
 * Maps to the `queues` table; separate from the `queue` item table.
 */
typedef struct ca_queue {
    int64_t id;
    cx_string *name;
    int32_t repeat_mode;
    bool temp;
    int64_t library_id;
} ca_queue;

CAUDIO_API ca_status ca_queue_init(ca_queue *queue,
                                   const cx_allocator *alloc);
CAUDIO_API ca_status ca_queue_copy(ca_queue *dst, const ca_queue *src,
                                   const cx_allocator *alloc);
CAUDIO_API void ca_queue_move(ca_queue *dst, ca_queue *src);
CAUDIO_API void ca_queue_destroy(ca_queue *queue);

/**
 * @brief Library (scan root) row.
 *
 * `extensions` defaults to "mp3,flac,ogg,wav,m4a"; `auto_scan` and
 * `recursive` default to true.
 */
typedef struct ca_library {
    int64_t id;
    cx_string *path;
    cx_string *name;
    int64_t date_added;
    int64_t last_scanned;
    bool auto_scan;
    bool recursive;
    cx_string *extensions;
} ca_library;

CAUDIO_API ca_status ca_library_init(ca_library *library,
                                     const cx_allocator *alloc);
CAUDIO_API ca_status ca_library_copy(ca_library *dst, const ca_library *src,
                                     const cx_allocator *alloc);
CAUDIO_API void ca_library_move(ca_library *dst, ca_library *src);
CAUDIO_API void ca_library_destroy(ca_library *library);

/**
 * @brief Aggregate database statistics.
 */
typedef struct ca_playback_stats {
    int64_t num_tracks;
    int64_t num_playlists;
    int64_t num_queue_items;
    int64_t num_history;
    int64_t num_bookmarks;
    int64_t num_libraries;
    int64_t total_duration_ms;
} ca_playback_stats;

CAUDIO_API ca_status ca_playback_stats_init(ca_playback_stats *stats,
                                            const cx_allocator *alloc);
CAUDIO_API ca_status ca_playback_stats_copy(ca_playback_stats *dst,
                                            const ca_playback_stats *src,
                                            const cx_allocator *alloc);
CAUDIO_API void ca_playback_stats_move(ca_playback_stats *dst,
                                       ca_playback_stats *src);
CAUDIO_API void ca_playback_stats_destroy(ca_playback_stats *stats);

/**
 * @brief Filter for listing tracks.
 *
 * All fields combine with AND. Empty strings and disengaged optional fields
 * are not applied. `limit == 0` means no limit.
 */
typedef struct ca_track_query {
    cx_string *artist;
    cx_string *album;
    cx_string *genre;
    bool has_year;
    int32_t year;
    bool has_year_min;
    int32_t year_min;
    bool has_year_max;
    int32_t year_max;
    bool has_library_id;
    int64_t library_id;
    bool has_dirty;
    bool dirty;
    bool has_rating;
    int32_t rating;
    bool has_play_count_min;
    int64_t play_count_min;
    bool has_play_count_max;
    int64_t play_count_max;
    bool has_duration_min;
    double duration_min;
    bool has_duration_max;
    double duration_max;
    int limit;
    int offset;
    cx_string *search;
    cx_string *sort;
    cx_string *order;
} ca_track_query;

CAUDIO_API ca_status ca_track_query_init(ca_track_query *query,
                                         const cx_allocator *alloc);
CAUDIO_API ca_status ca_track_query_copy(ca_track_query *dst,
                                         const ca_track_query *src,
                                         const cx_allocator *alloc);
CAUDIO_API void ca_track_query_move(ca_track_query *dst, ca_track_query *src);
CAUDIO_API void ca_track_query_destroy(ca_track_query *query);

/**
 * @brief Filter for listing history entries.
 */
typedef struct ca_history_query {
    bool has_track_id;
    int64_t track_id;
    bool has_queue_id;
    int64_t queue_id;
    int limit;
    int offset;
} ca_history_query;

CAUDIO_API ca_status ca_history_query_init(ca_history_query *query,
                                           const cx_allocator *alloc);
CAUDIO_API ca_status ca_history_query_copy(ca_history_query *dst,
                                           const ca_history_query *src,
                                           const cx_allocator *alloc);
CAUDIO_API void ca_history_query_move(ca_history_query *dst,
                                      ca_history_query *src);
CAUDIO_API void ca_history_query_destroy(ca_history_query *query);

/**
 * @brief Event payload queued by the playback engine.
 */
typedef struct ca_engine_event {
    ca_engine_event_type type;
    int64_t track_id;
    int64_t queue_id;
    double position;
    double duration;
    cx_string *msg;
} ca_engine_event;

CAUDIO_API ca_status ca_engine_event_init(ca_engine_event *event,
                                          const cx_allocator *alloc);
CAUDIO_API ca_status ca_engine_event_copy(ca_engine_event *dst,
                                          const ca_engine_event *src,
                                          const cx_allocator *alloc);
CAUDIO_API void ca_engine_event_move(ca_engine_event *dst,
                                     ca_engine_event *src);
CAUDIO_API void ca_engine_event_destroy(ca_engine_event *event);

/**
 * @brief Persisted playback-engine state.
 *
 * `shuffle_perm` holds `int64_t` values.
 */
typedef struct ca_engine_state {
    int shuffle_enabled;
    ca_repeat_mode repeat_mode;
    int64_t cursor_pos;
    int64_t current_track_id;
    float volume;
    int64_t active_queue_id;
    float pre_mute_volume;
    int64_t position_ms;
    cx_vector *shuffle_perm;
} ca_engine_state;

CAUDIO_API ca_status ca_engine_state_init(ca_engine_state *state,
                                          const cx_allocator *alloc);
CAUDIO_API ca_status ca_engine_state_copy(ca_engine_state *dst,
                                          const ca_engine_state *src,
                                          const cx_allocator *alloc);
CAUDIO_API void ca_engine_state_move(ca_engine_state *dst,
                                     ca_engine_state *src);
CAUDIO_API void ca_engine_state_destroy(ca_engine_state *state);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_TYPES_RECORD_H */
