#ifndef CAUDIO_TYPES_JSON_H
#define CAUDIO_TYPES_JSON_H

/**
 * @file caudio/types/json.h
 * @brief JSON codecs for the caudio domain records.
 *
 * Every `_write_json` function emits exactly one JSON object into an existing
 * ca_json_writer. Pass @p key when the object is a member of the current
 * object, or NULL when it is an array element or the document root.
 *
 * Every `_from_json` function initializes @p out (storage need not be
 * initialized) from a JSON object and takes ownership of all allocations. On
 * failure @p out is left zeroed and nothing needs to be released. Missing or
 * mistyped members fall back to the same defaults as the C++ implementation:
 * numbers become 0, strings become empty, and booleans become false. The one
 * exception is `ca_track`'s `source.kind`, where an unknown spelling is
 * rejected with CA_INVALID_ARGUMENT.
 *
 * Field names and ordering match `kanariv::utils::trackToJson` and
 * `kanariv::ipc::toJson` for the records that cross the wire. Records that are
 * only persisted in SQLite use their column names.
 */

#include <caudio/api.h>
#include <caudio/runtime/json.h>
#include <caudio/runtime/status.h>
#include <caudio/types/record.h>

#include <cstdx/allocator.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Source identity --------------------------------------------------------- */

/**
 * @brief Write a source reference as `{kind, key, display_name, available}`.
 */
CAUDIO_API ca_status ca_source_ref_write_json(const ca_source_ref *ref,
                                              ca_json_writer *writer,
                                              const char *key, ca_error *err);

/**
 * @brief Initialize a source reference from a JSON object.
 */
CAUDIO_API ca_status ca_source_ref_from_json(ca_source_ref *ref,
                                             const ca_json_val *val,
                                             const cx_allocator *alloc,
                                             ca_error *err);

/* Track ------------------------------------------------------------------- */

/**
 * @brief Write a track using the canonical `trackToJson` field order.
 *
 * The embedded source is normalized first: for `CA_SOURCE_PATH` an empty key
 * or display name is filled from `path`, matching `normalizedSource`.
 */
CAUDIO_API ca_status ca_track_write_json(const ca_track *track,
                                         ca_json_writer *writer,
                                         const char *key, ca_error *err);

/**
 * @brief Initialize a track from a JSON object.
 *
 * Reads `library_id` with a default of 1 and normalizes an explicit 0 to 1.
 * A missing or malformed `fingerprint` hex string falls back to the
 * deterministic path digest used by the C++ importer.
 */
CAUDIO_API ca_status ca_track_from_json(ca_track *track,
                                        const ca_json_val *val,
                                        const cx_allocator *alloc,
                                        ca_error *err);

/* Playlist ---------------------------------------------------------------- */

CAUDIO_API ca_status ca_playlist_write_json(const ca_playlist *playlist,
                                            ca_json_writer *writer,
                                            const char *key, ca_error *err);
CAUDIO_API ca_status ca_playlist_from_json(ca_playlist *playlist,
                                           const ca_json_val *val,
                                           const cx_allocator *alloc,
                                           ca_error *err);

/* Queue item -------------------------------------------------------------- */

CAUDIO_API ca_status ca_queue_item_write_json(const ca_queue_item *item,
                                              ca_json_writer *writer,
                                              const char *key, ca_error *err);
CAUDIO_API ca_status ca_queue_item_from_json(ca_queue_item *item,
                                             const ca_json_val *val,
                                             const cx_allocator *alloc,
                                             ca_error *err);

/* History entry ----------------------------------------------------------- */

CAUDIO_API ca_status ca_history_entry_write_json(
    const ca_history_entry *entry, ca_json_writer *writer, const char *key,
    ca_error *err);
CAUDIO_API ca_status ca_history_entry_from_json(ca_history_entry *entry,
                                                const ca_json_val *val,
                                                const cx_allocator *alloc,
                                                ca_error *err);

/* Bookmark ---------------------------------------------------------------- */

CAUDIO_API ca_status ca_bookmark_write_json(const ca_bookmark *bookmark,
                                            ca_json_writer *writer,
                                            const char *key, ca_error *err);
CAUDIO_API ca_status ca_bookmark_from_json(ca_bookmark *bookmark,
                                           const ca_json_val *val,
                                           const cx_allocator *alloc,
                                           ca_error *err);

/* Devices ----------------------------------------------------------------- */

/**
 * @brief Write a device using the IPC spelling `isDefault`.
 */
CAUDIO_API ca_status ca_device_info_write_json(const ca_device_info *device,
                                               ca_json_writer *writer,
                                               const char *key,
                                               ca_error *err);
CAUDIO_API ca_status ca_device_info_from_json(ca_device_info *device,
                                              const ca_json_val *val,
                                              const cx_allocator *alloc,
                                              ca_error *err);

/**
 * @brief Write a device list as `{"devices": [...]}`.
 */
CAUDIO_API ca_status ca_device_list_write_json(const ca_device_list *list,
                                               ca_json_writer *writer,
                                               const char *key,
                                               ca_error *err);
CAUDIO_API ca_status ca_device_list_from_json(ca_device_list *list,
                                              const ca_json_val *val,
                                              const cx_allocator *alloc,
                                              ca_error *err);

/* Metadata ---------------------------------------------------------------- */

CAUDIO_API ca_status ca_track_metadata_write_json(
    const ca_track_metadata *metadata, ca_json_writer *writer,
    const char *key, ca_error *err);
CAUDIO_API ca_status ca_track_metadata_from_json(ca_track_metadata *metadata,
                                                 const ca_json_val *val,
                                                 const cx_allocator *alloc,
                                                 ca_error *err);

/* Top entity -------------------------------------------------------------- */

CAUDIO_API ca_status ca_top_entity_write_json(const ca_top_entity *entity,
                                              ca_json_writer *writer,
                                              const char *key, ca_error *err);
CAUDIO_API ca_status ca_top_entity_from_json(ca_top_entity *entity,
                                             const ca_json_val *val,
                                             const cx_allocator *alloc,
                                             ca_error *err);

/* Library stats ----------------------------------------------------------- */

/**
 * @brief Write library statistics.
 *
 * A `top_*` array is emitted only when the corresponding `has_*` flag is set;
 * the arrays use the same element encodings as the IPC `LibraryStats` result.
 */
CAUDIO_API ca_status ca_library_stats_write_json(
    const ca_library_stats *stats, ca_json_writer *writer, const char *key,
    ca_error *err);
CAUDIO_API ca_status ca_library_stats_from_json(ca_library_stats *stats,
                                                const ca_json_val *val,
                                                const cx_allocator *alloc,
                                                ca_error *err);

/* Queue ------------------------------------------------------------------- */

CAUDIO_API ca_status ca_queue_write_json(const ca_queue *queue,
                                         ca_json_writer *writer,
                                         const char *key, ca_error *err);
CAUDIO_API ca_status ca_queue_from_json(ca_queue *queue,
                                        const ca_json_val *val,
                                        const cx_allocator *alloc,
                                        ca_error *err);

/* Library ----------------------------------------------------------------- */

CAUDIO_API ca_status ca_library_write_json(const ca_library *library,
                                           ca_json_writer *writer,
                                           const char *key, ca_error *err);
CAUDIO_API ca_status ca_library_from_json(ca_library *library,
                                          const ca_json_val *val,
                                          const cx_allocator *alloc,
                                          ca_error *err);

/* Playback statistics ----------------------------------------------------- */

CAUDIO_API ca_status ca_playback_stats_write_json(
    const ca_playback_stats *stats, ca_json_writer *writer, const char *key,
    ca_error *err);
CAUDIO_API ca_status ca_playback_stats_from_json(ca_playback_stats *stats,
                                                 const ca_json_val *val,
                                                 const cx_allocator *alloc,
                                                 ca_error *err);

/* Track query ------------------------------------------------------------- */

/**
 * @brief Write a track filter.
 *
 * Optional filters are written as `null` when their `has_*` flag is clear, so
 * the document distinguishes "not set" from a zero value.
 */
CAUDIO_API ca_status ca_track_query_write_json(const ca_track_query *query,
                                               ca_json_writer *writer,
                                               const char *key,
                                               ca_error *err);
CAUDIO_API ca_status ca_track_query_from_json(ca_track_query *query,
                                              const ca_json_val *val,
                                              const cx_allocator *alloc,
                                              ca_error *err);

/* History query ----------------------------------------------------------- */

CAUDIO_API ca_status ca_history_query_write_json(
    const ca_history_query *query, ca_json_writer *writer, const char *key,
    ca_error *err);
CAUDIO_API ca_status ca_history_query_from_json(ca_history_query *query,
                                                const ca_json_val *val,
                                                const cx_allocator *alloc,
                                                ca_error *err);

/* Engine event ------------------------------------------------------------ */

/**
 * @brief Write an engine event.
 *
 * `type` is the wire spelling from ca_engine_event_type_name().
 */
CAUDIO_API ca_status ca_engine_event_write_json(const ca_engine_event *event,
                                                ca_json_writer *writer,
                                                const char *key,
                                                ca_error *err);
CAUDIO_API ca_status ca_engine_event_from_json(ca_engine_event *event,
                                               const ca_json_val *val,
                                               const cx_allocator *alloc,
                                               ca_error *err);

/* Engine state ------------------------------------------------------------ */

/**
 * @brief Write persisted engine state.
 *
 * `shuffle_perm` is an array of track indices. It mirrors the SQLite
 * `engine_state` columns; `repeat_mode` is written as its numeric value.
 */
CAUDIO_API ca_status ca_engine_state_write_json(const ca_engine_state *state,
                                                ca_json_writer *writer,
                                                const char *key,
                                                ca_error *err);
CAUDIO_API ca_status ca_engine_state_from_json(ca_engine_state *state,
                                               const ca_json_val *val,
                                               const cx_allocator *alloc,
                                               ca_error *err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_TYPES_JSON_H */
