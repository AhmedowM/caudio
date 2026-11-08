#include <caudio/types/record.h>

#include "internal.h"

#include <cstdx/allocator.h>
#include <cstdx/string_view.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Internal helpers                                                          */
/* ------------------------------------------------------------------------- */

static ca_status ca_from_cx(cx_status status) {
    switch (status) {
    case CX_OK:
        return CA_OK;
    case CX_OUT_OF_MEMORY:
        return CA_OUT_OF_MEMORY;
    case CX_INVALID_ARGUMENT:
        return CA_INVALID_ARGUMENT;
    case CX_OVERFLOW:
        return CA_OVERFLOW;
    case CX_OUT_OF_RANGE:
        return CA_INVALID_ARGUMENT;
    default:
        return CA_INVALID_ARGUMENT;
    }
}

/*
 * cx_vector copy callbacks cannot report allocation failures. The record copy
 * routines stash the allocator and the first failure in thread-local state;
 * the enclosing copy checks it after every push. All copies in one call chain
 * share the same allocator.
 */
thread_local const cx_allocator *ca_impl_copy_alloc;
thread_local ca_status ca_impl_copy_status;

void ca_impl_copy_reset(const cx_allocator *alloc) {
    ca_impl_copy_alloc = alloc ? alloc : cx_allocator_default();
    ca_impl_copy_status = CA_OK;
}

void ca_impl_copy_fail(ca_status status) {
    if (status != CA_OK && ca_impl_copy_status == CA_OK) {
        ca_impl_copy_status = status;
    }
}

static ca_status ca_impl_str_init(cx_string **slot,
                                  const cx_allocator *alloc) {
    return ca_from_cx(cx_string_init(slot, alloc));
}

static ca_status ca_impl_str_assign(cx_string *dst, const cx_string *src) {
    if (!dst) {
        return CA_INVALID_ARGUMENT;
    }
    if (!src) {
        return CA_OK;
    }
    return ca_from_cx(cx_string_assign_view(dst, cx_string_view_of(src)));
}

static ca_status ca_impl_str_set(cx_string *dst, const char *text) {
    return ca_from_cx(
        cx_string_assign_view(dst, cx_string_view_from_cstr(text)));
}

static cx_string_view ca_impl_view(const cx_string *str) {
    return str ? cx_string_view_of(str) : cx_string_view_from_parts("", 0);
}

static ca_status ca_impl_vector_copy(cx_vector **dst, const cx_vector *src,
                                     size_t element_size,
                                     size_t element_align,
                                     const cx_vector_ops *ops,
                                     const cx_allocator *alloc) {
    *dst = nullptr;
    ca_status status = ca_from_cx(
        cx_vector_init(dst, element_size, element_align, ops, alloc));
    if (status != CA_OK) {
        return status;
    }
    if (!src) {
        return CA_OK;
    }

    ca_impl_copy_reset(alloc);
    size_t count = cx_vector_length(src);
    for (size_t i = 0; i < count; ++i) {
        status = ca_from_cx(cx_vector_push_copy(
            *dst, cx_vector_ptr(src, i), element_size));
        if (status == CA_OK && ca_impl_copy_status != CA_OK) {
            status = ca_impl_copy_status;
        }
        if (status != CA_OK) {
            cx_vector_destroy(dst);
            return status;
        }
    }
    return CA_OK;
}

/* ------------------------------------------------------------------------- */
/* Source identity                                                           */
/* ------------------------------------------------------------------------- */

ca_status ca_source_ref_init(ca_source_ref *ref, const cx_allocator *alloc) {
    if (!ref) {
        return CA_INVALID_ARGUMENT;
    }
    memset(ref, 0, sizeof *ref);
    ref->kind = CA_SOURCE_PATH;
    ref->available = true;

    ca_status status = ca_impl_str_init(&ref->key, alloc);
    if (status == CA_OK) {
        status = ca_impl_str_init(&ref->display_name, alloc);
    }
    if (status != CA_OK) {
        ca_source_ref_destroy(ref);
        return status;
    }
    return CA_OK;
}

static ca_status ca_impl_source_ref_assign(ca_source_ref *dst,
                                           const ca_source_ref *src) {
    dst->kind = src->kind;
    dst->available = src->available;
    ca_status status = ca_impl_str_assign(dst->key, src->key);
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->display_name, src->display_name);
    }
    return status;
}

ca_status ca_source_ref_copy(ca_source_ref *dst, const ca_source_ref *src,
                             const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);
    ca_status status = ca_source_ref_init(dst, alloc);
    if (status == CA_OK) {
        status = ca_impl_source_ref_assign(dst, src);
    }
    if (status != CA_OK) {
        ca_source_ref_destroy(dst);
    }
    return status;
}

void ca_source_ref_move(ca_source_ref *dst, ca_source_ref *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_source_ref_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_source_ref_destroy(ca_source_ref *ref) {
    if (!ref) {
        return;
    }
    cx_string_destroy(&ref->key);
    cx_string_destroy(&ref->display_name);
    memset(ref, 0, sizeof *ref);
}

/* ------------------------------------------------------------------------- */
/* Track                                                                     */
/* ------------------------------------------------------------------------- */

ca_status ca_track_init(ca_track *track, const cx_allocator *alloc) {
    if (!track) {
        return CA_INVALID_ARGUMENT;
    }
    memset(track, 0, sizeof *track);
    track->library_id = 1;

    cx_string **const slots[] = {
        &track->path,       &track->title, &track->artist,
        &track->album,      &track->album_artist, &track->genre,
        &track->cover_art_path,
    };
    ca_status status = CA_OK;
    for (size_t i = 0; i < sizeof slots / sizeof slots[0]; ++i) {
        status = ca_impl_str_init(slots[i], alloc);
        if (status != CA_OK) {
            ca_track_destroy(track);
            return status;
        }
    }

    status = ca_source_ref_init(&track->source, alloc);
    if (status != CA_OK) {
        ca_track_destroy(track);
    }
    return status;
}

ca_status ca_track_copy(ca_track *dst, const ca_track *src,
                        const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);
    ca_status status = ca_track_init(dst, alloc);
    if (status != CA_OK) {
        return status;
    }

    dst->id = src->id;
    dst->size = src->size;
    dst->mtime = src->mtime;
    dst->duration = src->duration;
    dst->play_count = src->play_count;
    dst->last_played = src->last_played;
    dst->date_added = src->date_added;
    dst->last_scanned = src->last_scanned;
    dst->deleted_at = src->deleted_at;
    dst->library_id = src->library_id;
    dst->sample_rate = src->sample_rate;
    dst->channels = src->channels;
    dst->bitrate = src->bitrate;
    dst->year = src->year;
    dst->track_num = src->track_num;
    dst->disc_num = src->disc_num;
    dst->rating = src->rating;
    dst->dirty = src->dirty;
    memcpy(dst->fingerprint, src->fingerprint, sizeof dst->fingerprint);

    status = ca_impl_str_assign(dst->path, src->path);
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->title, src->title);
    }
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->artist, src->artist);
    }
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->album, src->album);
    }
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->album_artist, src->album_artist);
    }
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->genre, src->genre);
    }
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->cover_art_path, src->cover_art_path);
    }
    if (status == CA_OK) {
        status = ca_impl_source_ref_assign(&dst->source, &src->source);
    }
    if (status != CA_OK) {
        ca_track_destroy(dst);
    }
    return status;
}

void ca_track_move(ca_track *dst, ca_track *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_track_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_track_destroy(ca_track *track) {
    if (!track) {
        return;
    }
    cx_string_destroy(&track->path);
    cx_string_destroy(&track->title);
    cx_string_destroy(&track->artist);
    cx_string_destroy(&track->album);
    cx_string_destroy(&track->album_artist);
    cx_string_destroy(&track->genre);
    cx_string_destroy(&track->cover_art_path);
    ca_source_ref_destroy(&track->source);
    memset(track, 0, sizeof *track);
}

ca_status ca_track_normalized_source(const ca_track *track,
                                     ca_source_ref *out,
                                     const cx_allocator *alloc) {
    if (!track || !out) {
        return CA_INVALID_ARGUMENT;
    }
    ca_status status = ca_source_ref_copy(out, &track->source, alloc);
    if (status != CA_OK) {
        return status;
    }
    if (out->kind == CA_SOURCE_PATH) {
        if (cx_string_empty(out->key)) {
            status = ca_from_cx(
                cx_string_assign_view(out->key, ca_impl_view(track->path)));
        }
        if (status == CA_OK && cx_string_empty(out->display_name)) {
            status = ca_from_cx(cx_string_assign_view(
                out->display_name, ca_impl_view(track->path)));
        }
    }
    if (status != CA_OK) {
        ca_source_ref_destroy(out);
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/* Playlist                                                                  */
/* ------------------------------------------------------------------------- */

ca_status ca_playlist_init(ca_playlist *playlist, const cx_allocator *alloc) {
    if (!playlist) {
        return CA_INVALID_ARGUMENT;
    }
    memset(playlist, 0, sizeof *playlist);
    playlist->library_id = 1;

    ca_status status = ca_impl_str_init(&playlist->name, alloc);
    if (status == CA_OK) {
        status = ca_impl_str_init(&playlist->smart_query, alloc);
    }
    if (status != CA_OK) {
        ca_playlist_destroy(playlist);
    }
    return status;
}

ca_status ca_playlist_copy(ca_playlist *dst, const ca_playlist *src,
                           const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);
    ca_status status = ca_playlist_init(dst, alloc);
    if (status != CA_OK) {
        return status;
    }
    dst->id = src->id;
    dst->type = src->type;
    dst->created = src->created;
    dst->modified = src->modified;
    dst->library_id = src->library_id;
    status = ca_impl_str_assign(dst->name, src->name);
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->smart_query, src->smart_query);
    }
    if (status != CA_OK) {
        ca_playlist_destroy(dst);
    }
    return status;
}

void ca_playlist_move(ca_playlist *dst, ca_playlist *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_playlist_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_playlist_destroy(ca_playlist *playlist) {
    if (!playlist) {
        return;
    }
    cx_string_destroy(&playlist->name);
    cx_string_destroy(&playlist->smart_query);
    memset(playlist, 0, sizeof *playlist);
}

/* ------------------------------------------------------------------------- */
/* Queue item                                                                */
/* ------------------------------------------------------------------------- */

ca_status ca_queue_item_init(ca_queue_item *item, const cx_allocator *alloc) {
    (void)alloc;
    if (!item) {
        return CA_INVALID_ARGUMENT;
    }
    memset(item, 0, sizeof *item);
    item->queue_id = 1;
    return CA_OK;
}

ca_status ca_queue_item_copy(ca_queue_item *dst, const ca_queue_item *src,
                             const cx_allocator *alloc) {
    (void)alloc;
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    *dst = *src;
    return CA_OK;
}

void ca_queue_item_move(ca_queue_item *dst, ca_queue_item *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_queue_item_destroy(ca_queue_item *item) {
    if (item) {
        memset(item, 0, sizeof *item);
    }
}

/* ------------------------------------------------------------------------- */
/* History entry                                                             */
/* ------------------------------------------------------------------------- */

ca_status ca_history_entry_init(ca_history_entry *entry,
                                const cx_allocator *alloc) {
    if (!entry) {
        return CA_INVALID_ARGUMENT;
    }
    memset(entry, 0, sizeof *entry);
    entry->queue_id = 1;

    ca_status status = ca_impl_str_init(&entry->title, alloc);
    if (status == CA_OK) {
        status = ca_impl_str_init(&entry->artist, alloc);
    }
    if (status == CA_OK) {
        status = ca_impl_str_init(&entry->path, alloc);
    }
    if (status != CA_OK) {
        ca_history_entry_destroy(entry);
    }
    return status;
}

ca_status ca_history_entry_copy(ca_history_entry *dst,
                                const ca_history_entry *src,
                                const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);
    ca_status status = ca_history_entry_init(dst, alloc);
    if (status != CA_OK) {
        return status;
    }
    dst->id = src->id;
    dst->track_id = src->track_id;
    dst->started_at = src->started_at;
    dst->completed_at = src->completed_at;
    dst->position_ms = src->position_ms;
    dst->completion_pct = src->completion_pct;
    dst->queue_id = src->queue_id;
    dst->duration = src->duration;
    status = ca_impl_str_assign(dst->title, src->title);
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->artist, src->artist);
    }
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->path, src->path);
    }
    if (status != CA_OK) {
        ca_history_entry_destroy(dst);
    }
    return status;
}

void ca_history_entry_move(ca_history_entry *dst, ca_history_entry *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_history_entry_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_history_entry_destroy(ca_history_entry *entry) {
    if (!entry) {
        return;
    }
    cx_string_destroy(&entry->title);
    cx_string_destroy(&entry->artist);
    cx_string_destroy(&entry->path);
    memset(entry, 0, sizeof *entry);
}

/* ------------------------------------------------------------------------- */
/* Bookmark                                                                  */
/* ------------------------------------------------------------------------- */

ca_status ca_bookmark_init(ca_bookmark *bookmark, const cx_allocator *alloc) {
    if (!bookmark) {
        return CA_INVALID_ARGUMENT;
    }
    memset(bookmark, 0, sizeof *bookmark);
    ca_status status = ca_impl_str_init(&bookmark->note, alloc);
    if (status != CA_OK) {
        ca_bookmark_destroy(bookmark);
    }
    return status;
}

ca_status ca_bookmark_copy(ca_bookmark *dst, const ca_bookmark *src,
                           const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);
    ca_status status = ca_bookmark_init(dst, alloc);
    if (status != CA_OK) {
        return status;
    }
    dst->id = src->id;
    dst->track_id = src->track_id;
    dst->position_ms = src->position_ms;
    dst->created = src->created;
    status = ca_impl_str_assign(dst->note, src->note);
    if (status != CA_OK) {
        ca_bookmark_destroy(dst);
    }
    return status;
}

void ca_bookmark_move(ca_bookmark *dst, ca_bookmark *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_bookmark_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_bookmark_destroy(ca_bookmark *bookmark) {
    if (!bookmark) {
        return;
    }
    cx_string_destroy(&bookmark->note);
    memset(bookmark, 0, sizeof *bookmark);
}

/* ------------------------------------------------------------------------- */
/* Device info / list                                                        */
/* ------------------------------------------------------------------------- */

ca_status ca_device_info_init(ca_device_info *device,
                              const cx_allocator *alloc) {
    if (!device) {
        return CA_INVALID_ARGUMENT;
    }
    memset(device, 0, sizeof *device);
    ca_status status = ca_impl_str_init(&device->id, alloc);
    if (status == CA_OK) {
        status = ca_impl_str_init(&device->name, alloc);
    }
    if (status != CA_OK) {
        ca_device_info_destroy(device);
    }
    return status;
}

ca_status ca_device_info_copy(ca_device_info *dst, const ca_device_info *src,
                              const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);
    ca_status status = ca_device_info_init(dst, alloc);
    if (status != CA_OK) {
        return status;
    }
    dst->is_default = src->is_default;
    status = ca_impl_str_assign(dst->id, src->id);
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->name, src->name);
    }
    if (status != CA_OK) {
        ca_device_info_destroy(dst);
    }
    return status;
}

void ca_device_info_move(ca_device_info *dst, ca_device_info *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_device_info_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_device_info_destroy(ca_device_info *device) {
    if (!device) {
        return;
    }
    cx_string_destroy(&device->id);
    cx_string_destroy(&device->name);
    memset(device, 0, sizeof *device);
}

static void ca_device_info_vec_copy(void *dst, const void *src) {
    ca_impl_copy_fail(ca_device_info_copy(
        (ca_device_info *)dst, (const ca_device_info *)src,
        ca_impl_copy_alloc));
}

static void ca_device_info_vec_move(void *dst, void *src) {
    memcpy(dst, src, sizeof(ca_device_info));
    memset(src, 0, sizeof(ca_device_info));
}

static void ca_device_info_vec_destroy(void *element) {
    ca_device_info_destroy((ca_device_info *)element);
}

const cx_vector_ops ca_impl_device_info_vector_ops = {
    .copy = ca_device_info_vec_copy,
    .move = ca_device_info_vec_move,
    .destroy = ca_device_info_vec_destroy,
};

ca_status ca_device_list_init(ca_device_list *list, const cx_allocator *alloc) {
    if (!list) {
        return CA_INVALID_ARGUMENT;
    }
    memset(list, 0, sizeof *list);
    return ca_from_cx(cx_vector_init(&list->devices, sizeof(ca_device_info),
                                     alignof(ca_device_info),
                                     &ca_impl_device_info_vector_ops, alloc));
}

ca_status ca_device_list_copy(ca_device_list *dst, const ca_device_list *src,
                              const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);
    return ca_impl_vector_copy(&dst->devices, src->devices,
                               sizeof(ca_device_info),
                               alignof(ca_device_info), &ca_impl_device_info_vector_ops,
                               alloc);
}

void ca_device_list_move(ca_device_list *dst, ca_device_list *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_device_list_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_device_list_destroy(ca_device_list *list) {
    if (!list) {
        return;
    }
    cx_vector_destroy(&list->devices);
    memset(list, 0, sizeof *list);
}

/* ------------------------------------------------------------------------- */
/* Track metadata                                                            */
/* ------------------------------------------------------------------------- */

ca_status ca_track_metadata_init(ca_track_metadata *metadata,
                                 const cx_allocator *alloc) {
    if (!metadata) {
        return CA_INVALID_ARGUMENT;
    }
    memset(metadata, 0, sizeof *metadata);

    cx_string **const slots[] = {
        &metadata->title, &metadata->artist, &metadata->album,
        &metadata->album_artist, &metadata->genre,
    };
    ca_status status = CA_OK;
    for (size_t i = 0; i < sizeof slots / sizeof slots[0]; ++i) {
        status = ca_impl_str_init(slots[i], alloc);
        if (status != CA_OK) {
            ca_track_metadata_destroy(metadata);
            return status;
        }
    }
    return CA_OK;
}

ca_status ca_track_metadata_copy(ca_track_metadata *dst,
                                 const ca_track_metadata *src,
                                 const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);
    ca_status status = ca_track_metadata_init(dst, alloc);
    if (status != CA_OK) {
        return status;
    }
    dst->year = src->year;
    dst->track_num = src->track_num;
    dst->disc_num = src->disc_num;
    dst->duration = src->duration;
    dst->sample_rate = src->sample_rate;
    dst->channels = src->channels;
    dst->bitrate = src->bitrate;
    status = ca_impl_str_assign(dst->title, src->title);
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->artist, src->artist);
    }
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->album, src->album);
    }
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->album_artist, src->album_artist);
    }
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->genre, src->genre);
    }
    if (status != CA_OK) {
        ca_track_metadata_destroy(dst);
    }
    return status;
}

void ca_track_metadata_move(ca_track_metadata *dst,
                            ca_track_metadata *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_track_metadata_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_track_metadata_destroy(ca_track_metadata *metadata) {
    if (!metadata) {
        return;
    }
    cx_string_destroy(&metadata->title);
    cx_string_destroy(&metadata->artist);
    cx_string_destroy(&metadata->album);
    cx_string_destroy(&metadata->album_artist);
    cx_string_destroy(&metadata->genre);
    memset(metadata, 0, sizeof *metadata);
}

/* ------------------------------------------------------------------------- */
/* Top entity                                                                */
/* ------------------------------------------------------------------------- */

ca_status ca_top_entity_init(ca_top_entity *entity, const cx_allocator *alloc) {
    if (!entity) {
        return CA_INVALID_ARGUMENT;
    }
    memset(entity, 0, sizeof *entity);
    ca_status status = ca_impl_str_init(&entity->name, alloc);
    if (status != CA_OK) {
        ca_top_entity_destroy(entity);
    }
    return status;
}

ca_status ca_top_entity_copy(ca_top_entity *dst, const ca_top_entity *src,
                             const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);
    ca_status status = ca_top_entity_init(dst, alloc);
    if (status != CA_OK) {
        return status;
    }
    dst->track_count = src->track_count;
    dst->total_plays = src->total_plays;
    status = ca_impl_str_assign(dst->name, src->name);
    if (status != CA_OK) {
        ca_top_entity_destroy(dst);
    }
    return status;
}

void ca_top_entity_move(ca_top_entity *dst, ca_top_entity *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_top_entity_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_top_entity_destroy(ca_top_entity *entity) {
    if (!entity) {
        return;
    }
    cx_string_destroy(&entity->name);
    memset(entity, 0, sizeof *entity);
}

static void ca_top_entity_vec_copy(void *dst, const void *src) {
    ca_impl_copy_fail(ca_top_entity_copy((ca_top_entity *)dst,
                                         (const ca_top_entity *)src,
                                         ca_impl_copy_alloc));
}

static void ca_top_entity_vec_move(void *dst, void *src) {
    memcpy(dst, src, sizeof(ca_top_entity));
    memset(src, 0, sizeof(ca_top_entity));
}

static void ca_top_entity_vec_destroy(void *element) {
    ca_top_entity_destroy((ca_top_entity *)element);
}

const cx_vector_ops ca_impl_top_entity_vector_ops = {
    .copy = ca_top_entity_vec_copy,
    .move = ca_top_entity_vec_move,
    .destroy = ca_top_entity_vec_destroy,
};

static void ca_track_vec_copy(void *dst, const void *src) {
    ca_impl_copy_fail(ca_track_copy((ca_track *)dst, (const ca_track *)src,
                                    ca_impl_copy_alloc));
}

static void ca_track_vec_move(void *dst, void *src) {
    memcpy(dst, src, sizeof(ca_track));
    memset(src, 0, sizeof(ca_track));
}

static void ca_track_vec_destroy(void *element) {
    ca_track_destroy((ca_track *)element);
}

const cx_vector_ops ca_impl_track_vector_ops = {
    .copy = ca_track_vec_copy,
    .move = ca_track_vec_move,
    .destroy = ca_track_vec_destroy,
};

/* ------------------------------------------------------------------------- */
/* Library stats                                                             */
/* ------------------------------------------------------------------------- */

ca_status ca_library_stats_init(ca_library_stats *stats,
                                const cx_allocator *alloc) {
    (void)alloc;
    if (!stats) {
        return CA_INVALID_ARGUMENT;
    }
    memset(stats, 0, sizeof *stats);
    return CA_OK;
}

static ca_status ca_impl_copy_track_list(bool present, cx_vector **dst,
                                         const cx_vector *src,
                                         const cx_allocator *alloc) {
    *dst = nullptr;
    if (!present) {
        return CA_OK;
    }
    return ca_impl_vector_copy(dst, src, sizeof(ca_track),
                               alignof(ca_track), &ca_impl_track_vector_ops, alloc);
}

static ca_status ca_impl_copy_top_list(bool present, cx_vector **dst,
                                       const cx_vector *src,
                                       const cx_allocator *alloc) {
    *dst = nullptr;
    if (!present) {
        return CA_OK;
    }
    return ca_impl_vector_copy(dst, src, sizeof(ca_top_entity),
                               alignof(ca_top_entity), &ca_impl_top_entity_vector_ops,
                               alloc);
}

ca_status ca_library_stats_copy(ca_library_stats *dst,
                                const ca_library_stats *src,
                                const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);

    dst->tracks = src->tracks;
    dst->queues = src->queues;
    dst->playlists = src->playlists;
    dst->total_duration_ms = src->total_duration_ms;
    dst->total_play_time_ms = src->total_play_time_ms;
    dst->has_top_tracks = src->has_top_tracks;
    dst->has_top_albums = src->has_top_albums;
    dst->has_top_genres = src->has_top_genres;
    dst->has_top_artists = src->has_top_artists;

    ca_status status = ca_impl_copy_track_list(
        src->has_top_tracks, &dst->top_tracks, src->top_tracks, alloc);
    if (status == CA_OK) {
        status = ca_impl_copy_top_list(src->has_top_albums, &dst->top_albums,
                                       src->top_albums, alloc);
    }
    if (status == CA_OK) {
        status = ca_impl_copy_top_list(src->has_top_genres, &dst->top_genres,
                                       src->top_genres, alloc);
    }
    if (status == CA_OK) {
        status = ca_impl_copy_top_list(src->has_top_artists, &dst->top_artists,
                                       src->top_artists, alloc);
    }
    if (status != CA_OK) {
        ca_library_stats_destroy(dst);
    }
    return status;
}

void ca_library_stats_move(ca_library_stats *dst, ca_library_stats *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_library_stats_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_library_stats_destroy(ca_library_stats *stats) {
    if (!stats) {
        return;
    }
    cx_vector_destroy(&stats->top_tracks);
    cx_vector_destroy(&stats->top_albums);
    cx_vector_destroy(&stats->top_genres);
    cx_vector_destroy(&stats->top_artists);
    memset(stats, 0, sizeof *stats);
}

/* ------------------------------------------------------------------------- */
/* Queue                                                                     */
/* ------------------------------------------------------------------------- */

ca_status ca_queue_init(ca_queue *queue, const cx_allocator *alloc) {
    if (!queue) {
        return CA_INVALID_ARGUMENT;
    }
    memset(queue, 0, sizeof *queue);
    queue->library_id = 1;
    ca_status status = ca_impl_str_init(&queue->name, alloc);
    if (status != CA_OK) {
        ca_queue_destroy(queue);
    }
    return status;
}

ca_status ca_queue_copy(ca_queue *dst, const ca_queue *src,
                        const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);
    ca_status status = ca_queue_init(dst, alloc);
    if (status != CA_OK) {
        return status;
    }
    dst->id = src->id;
    dst->repeat_mode = src->repeat_mode;
    dst->temp = src->temp;
    dst->library_id = src->library_id;
    status = ca_impl_str_assign(dst->name, src->name);
    if (status != CA_OK) {
        ca_queue_destroy(dst);
    }
    return status;
}

void ca_queue_move(ca_queue *dst, ca_queue *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_queue_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_queue_destroy(ca_queue *queue) {
    if (!queue) {
        return;
    }
    cx_string_destroy(&queue->name);
    memset(queue, 0, sizeof *queue);
}

/* ------------------------------------------------------------------------- */
/* Library                                                                   */
/* ------------------------------------------------------------------------- */

ca_status ca_library_init(ca_library *library, const cx_allocator *alloc) {
    if (!library) {
        return CA_INVALID_ARGUMENT;
    }
    memset(library, 0, sizeof *library);
    library->auto_scan = true;
    library->recursive = true;

    ca_status status = ca_impl_str_init(&library->path, alloc);
    if (status == CA_OK) {
        status = ca_impl_str_init(&library->name, alloc);
    }
    if (status == CA_OK) {
        status = ca_impl_str_init(&library->extensions, alloc);
    }
    if (status == CA_OK) {
        status = ca_impl_str_set(library->extensions, "mp3,flac,ogg,wav,m4a");
    }
    if (status != CA_OK) {
        ca_library_destroy(library);
    }
    return status;
}

ca_status ca_library_copy(ca_library *dst, const ca_library *src,
                          const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);
    ca_status status = ca_library_init(dst, alloc);
    if (status != CA_OK) {
        return status;
    }
    dst->id = src->id;
    dst->date_added = src->date_added;
    dst->last_scanned = src->last_scanned;
    dst->auto_scan = src->auto_scan;
    dst->recursive = src->recursive;
    status = ca_impl_str_assign(dst->path, src->path);
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->name, src->name);
    }
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->extensions, src->extensions);
    }
    if (status != CA_OK) {
        ca_library_destroy(dst);
    }
    return status;
}

void ca_library_move(ca_library *dst, ca_library *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_library_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_library_destroy(ca_library *library) {
    if (!library) {
        return;
    }
    cx_string_destroy(&library->path);
    cx_string_destroy(&library->name);
    cx_string_destroy(&library->extensions);
    memset(library, 0, sizeof *library);
}

/* ------------------------------------------------------------------------- */
/* Playback stats                                                            */
/* ------------------------------------------------------------------------- */

ca_status ca_playback_stats_init(ca_playback_stats *stats,
                                 const cx_allocator *alloc) {
    (void)alloc;
    if (!stats) {
        return CA_INVALID_ARGUMENT;
    }
    memset(stats, 0, sizeof *stats);
    return CA_OK;
}

ca_status ca_playback_stats_copy(ca_playback_stats *dst,
                                 const ca_playback_stats *src,
                                 const cx_allocator *alloc) {
    (void)alloc;
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    *dst = *src;
    return CA_OK;
}

void ca_playback_stats_move(ca_playback_stats *dst, ca_playback_stats *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_playback_stats_destroy(ca_playback_stats *stats) {
    if (stats) {
        memset(stats, 0, sizeof *stats);
    }
}

/* ------------------------------------------------------------------------- */
/* Track query                                                               */
/* ------------------------------------------------------------------------- */

ca_status ca_track_query_init(ca_track_query *query,
                              const cx_allocator *alloc) {
    if (!query) {
        return CA_INVALID_ARGUMENT;
    }
    memset(query, 0, sizeof *query);

    cx_string **const slots[] = {
        &query->artist, &query->album, &query->genre,
        &query->search, &query->sort, &query->order,
    };
    ca_status status = CA_OK;
    for (size_t i = 0; i < sizeof slots / sizeof slots[0]; ++i) {
        status = ca_impl_str_init(slots[i], alloc);
        if (status != CA_OK) {
            ca_track_query_destroy(query);
            return status;
        }
    }
    return CA_OK;
}

ca_status ca_track_query_copy(ca_track_query *dst, const ca_track_query *src,
                              const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);
    ca_status status = ca_track_query_init(dst, alloc);
    if (status != CA_OK) {
        return status;
    }

    dst->has_year = src->has_year;
    dst->year = src->year;
    dst->has_year_min = src->has_year_min;
    dst->year_min = src->year_min;
    dst->has_year_max = src->has_year_max;
    dst->year_max = src->year_max;
    dst->has_library_id = src->has_library_id;
    dst->library_id = src->library_id;
    dst->has_dirty = src->has_dirty;
    dst->dirty = src->dirty;
    dst->has_rating = src->has_rating;
    dst->rating = src->rating;
    dst->has_play_count_min = src->has_play_count_min;
    dst->play_count_min = src->play_count_min;
    dst->has_play_count_max = src->has_play_count_max;
    dst->play_count_max = src->play_count_max;
    dst->has_duration_min = src->has_duration_min;
    dst->duration_min = src->duration_min;
    dst->has_duration_max = src->has_duration_max;
    dst->duration_max = src->duration_max;
    dst->limit = src->limit;
    dst->offset = src->offset;

    status = ca_impl_str_assign(dst->artist, src->artist);
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->album, src->album);
    }
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->genre, src->genre);
    }
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->search, src->search);
    }
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->sort, src->sort);
    }
    if (status == CA_OK) {
        status = ca_impl_str_assign(dst->order, src->order);
    }
    if (status != CA_OK) {
        ca_track_query_destroy(dst);
    }
    return status;
}

void ca_track_query_move(ca_track_query *dst, ca_track_query *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_track_query_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_track_query_destroy(ca_track_query *query) {
    if (!query) {
        return;
    }
    cx_string_destroy(&query->artist);
    cx_string_destroy(&query->album);
    cx_string_destroy(&query->genre);
    cx_string_destroy(&query->search);
    cx_string_destroy(&query->sort);
    cx_string_destroy(&query->order);
    memset(query, 0, sizeof *query);
}

/* ------------------------------------------------------------------------- */
/* History query                                                             */
/* ------------------------------------------------------------------------- */

ca_status ca_history_query_init(ca_history_query *query,
                                const cx_allocator *alloc) {
    (void)alloc;
    if (!query) {
        return CA_INVALID_ARGUMENT;
    }
    memset(query, 0, sizeof *query);
    return CA_OK;
}

ca_status ca_history_query_copy(ca_history_query *dst,
                                const ca_history_query *src,
                                const cx_allocator *alloc) {
    (void)alloc;
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    *dst = *src;
    return CA_OK;
}

void ca_history_query_move(ca_history_query *dst, ca_history_query *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_history_query_destroy(ca_history_query *query) {
    if (query) {
        memset(query, 0, sizeof *query);
    }
}

/* ------------------------------------------------------------------------- */
/* Engine event                                                              */
/* ------------------------------------------------------------------------- */

ca_status ca_engine_event_init(ca_engine_event *event,
                               const cx_allocator *alloc) {
    if (!event) {
        return CA_INVALID_ARGUMENT;
    }
    memset(event, 0, sizeof *event);
    event->type = CA_ENGINE_EVENT_NONE;
    event->queue_id = 1;
    ca_status status = ca_impl_str_init(&event->msg, alloc);
    if (status != CA_OK) {
        ca_engine_event_destroy(event);
    }
    return status;
}

ca_status ca_engine_event_copy(ca_engine_event *dst,
                               const ca_engine_event *src,
                               const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);
    ca_status status = ca_engine_event_init(dst, alloc);
    if (status != CA_OK) {
        return status;
    }
    dst->type = src->type;
    dst->track_id = src->track_id;
    dst->queue_id = src->queue_id;
    dst->position = src->position;
    dst->duration = src->duration;
    status = ca_impl_str_assign(dst->msg, src->msg);
    if (status != CA_OK) {
        ca_engine_event_destroy(dst);
    }
    return status;
}

void ca_engine_event_move(ca_engine_event *dst, ca_engine_event *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_engine_event_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_engine_event_destroy(ca_engine_event *event) {
    if (!event) {
        return;
    }
    cx_string_destroy(&event->msg);
    memset(event, 0, sizeof *event);
}

/* ------------------------------------------------------------------------- */
/* Engine state                                                              */
/* ------------------------------------------------------------------------- */

ca_status ca_engine_state_init(ca_engine_state *state,
                               const cx_allocator *alloc) {
    if (!state) {
        return CA_INVALID_ARGUMENT;
    }
    memset(state, 0, sizeof *state);
    state->repeat_mode = CA_REPEAT_OFF;
    state->volume = 1.0f;
    state->active_queue_id = 1;
    state->pre_mute_volume = 0.5f;

    return ca_from_cx(cx_vector_init(&state->shuffle_perm, sizeof(int64_t),
                                     alignof(int64_t), nullptr, alloc));
}

ca_status ca_engine_state_copy(ca_engine_state *dst,
                               const ca_engine_state *src,
                               const cx_allocator *alloc) {
    if (!dst || !src) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof *dst);
    dst->shuffle_enabled = src->shuffle_enabled;
    dst->repeat_mode = src->repeat_mode;
    dst->cursor_pos = src->cursor_pos;
    dst->current_track_id = src->current_track_id;
    dst->volume = src->volume;
    dst->active_queue_id = src->active_queue_id;
    dst->pre_mute_volume = src->pre_mute_volume;
    dst->position_ms = src->position_ms;

    return ca_impl_vector_copy(&dst->shuffle_perm, src->shuffle_perm,
                               sizeof(int64_t), alignof(int64_t), nullptr,
                               alloc);
}

void ca_engine_state_move(ca_engine_state *dst, ca_engine_state *src) {
    if (!dst || !src || dst == src) {
        return;
    }
    ca_engine_state_destroy(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
}

void ca_engine_state_destroy(ca_engine_state *state) {
    if (!state) {
        return;
    }
    cx_vector_destroy(&state->shuffle_perm);
    memset(state, 0, sizeof *state);
}
