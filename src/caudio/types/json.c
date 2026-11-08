#include <caudio/types/json.h>

#include "internal.h"

#include <cstdx/string.h>
#include <cstdx/string_view.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Fail-fast helper for the writer calls: every helper returns ca_status. */
#define CA_JSON_TRY(expr)                                                     \
    do {                                                                      \
        ca_status ca_json_try_status = (expr);                                \
        if (ca_json_try_status != CA_OK) {                                    \
            return ca_json_try_status;                                        \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------------------- */
/* Status and string helpers                                                 */
/* ------------------------------------------------------------------------- */

static ca_status json_from_cx(cx_status status, ca_error *err) {
    switch (status) {
    case CX_OK:
        return CA_OK;
    case CX_OUT_OF_MEMORY:
        if (err) {
            ca_error_set(err, CA_OUT_OF_MEMORY, "record JSON codec is out of memory");
        }
        return CA_OUT_OF_MEMORY;
    case CX_OVERFLOW:
        return CA_OVERFLOW;
    case CX_INVALID_ARGUMENT:
        return CA_INVALID_ARGUMENT;
    default:
        return CA_INVALID_ARGUMENT;
    }
}

static cx_string_view json_empty_view(void) {
    return cx_string_view_from_parts("", 0);
}

static cx_string_view json_str_view(const cx_string *str) {
    return str ? cx_string_view_of(str) : json_empty_view();
}

static ca_status json_add_str(ca_json_writer *writer, const char *key,
                              const cx_string *value, ca_error *err) {
    cx_string_view view = json_str_view(value);
    return ca_json_writer_add_string(writer, key, view.data, view.length, err);
}

static ca_status json_assign_view(cx_string *dst, cx_string_view view,
                                  ca_error *err) {
    if (!dst) {
        return CA_OK;
    }
    return json_from_cx(cx_string_assign_view(dst, view), err);
}

/* ------------------------------------------------------------------------- */
/* Parsed-value helpers                                                      */
/* ------------------------------------------------------------------------- */

/*
 * ca_json_get_int()/ca_json_get_double() are subtype-strict, but a source
 * document may spell the same number either way (and the C++ implementation
 * accepts any number for a numeric member). Normalize here.
 */
static bool json_number_int(const ca_json_val *val, int64_t *out) {
    switch (ca_json_type_of(val)) {
    case CA_JSON_INT:
        *out = ca_json_get_int(val);
        return true;
    case CA_JSON_UINT:
        *out = (int64_t)ca_json_get_uint(val);
        return true;
    case CA_JSON_DOUBLE:
        *out = (int64_t)ca_json_get_double(val);
        return true;
    default:
        return false;
    }
}

static bool json_number_double(const ca_json_val *val, double *out) {
    switch (ca_json_type_of(val)) {
    case CA_JSON_INT:
        *out = (double)ca_json_get_int(val);
        return true;
    case CA_JSON_UINT:
        *out = (double)ca_json_get_uint(val);
        return true;
    case CA_JSON_DOUBLE:
        *out = ca_json_get_double(val);
        return true;
    default:
        return false;
    }
}

static bool json_is_object(const ca_json_val *val) {
    return val && ca_json_type_of(val) == CA_JSON_OBJECT;
}

static bool json_is_array(const ca_json_val *val) {
    return val && ca_json_type_of(val) == CA_JSON_ARRAY;
}

static int64_t json_int_member(const ca_json_val *obj, const char *key,
                               int64_t fallback) {
    const ca_json_val *val = ca_json_object_get(obj, key);
    int64_t value = 0;
    return json_number_int(val, &value) ? value : fallback;
}

static int32_t json_i32_member(const ca_json_val *obj, const char *key,
                               int32_t fallback) {
    return (int32_t)json_int_member(obj, key, fallback);
}

static double json_double_member(const ca_json_val *obj, const char *key,
                                 double fallback) {
    const ca_json_val *val = ca_json_object_get(obj, key);
    double value = 0.0;
    return json_number_double(val, &value) ? value : fallback;
}

static bool json_bool_member(const ca_json_val *obj, const char *key,
                             bool fallback) {
    const ca_json_val *val = ca_json_object_get(obj, key);
    ca_json_type type = ca_json_type_of(val);
    if (type == CA_JSON_TRUE) {
        return true;
    }
    if (type == CA_JSON_FALSE) {
        return false;
    }
    return fallback;
}

static ca_status json_read_str(const ca_json_val *obj, const char *key,
                               cx_string *dst, ca_error *err) {
    const ca_json_val *val = ca_json_object_get(obj, key);
    cx_string_view view = json_empty_view();
    if (val && ca_json_type_of(val) == CA_JSON_STRING) {
        size_t length = 0;
        const char *data = ca_json_get_string(val, &length);
        view = cx_string_view_from_parts(data ? data : "", length);
    }
    return json_assign_view(dst, view, err);
}

static void json_read_optional_int(const ca_json_val *obj, const char *key,
                                   bool *has, int64_t *out) {
    const ca_json_val *val = ca_json_object_get(obj, key);
    int64_t value = 0;
    if (json_number_int(val, &value)) {
        *has = true;
        *out = value;
    } else {
        *has = false;
        *out = 0;
    }
}

static void json_read_optional_bool(const ca_json_val *obj, const char *key,
                                    bool *has, bool *out) {
    const ca_json_val *val = ca_json_object_get(obj, key);
    ca_json_type type = ca_json_type_of(val);
    if (type == CA_JSON_TRUE || type == CA_JSON_FALSE) {
        *has = true;
        *out = (type == CA_JSON_TRUE);
    } else {
        *has = false;
        *out = false;
    }
}

/* ------------------------------------------------------------------------- */
/* Fingerprint helpers (byte-identical to kanariv utils)                     */
/* ------------------------------------------------------------------------- */

static int json_hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void json_fingerprint_to_hex(const uint8_t fingerprint[32],
                                    char out[65]) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        out[i * 2] = digits[fingerprint[i] >> 4];
        out[i * 2 + 1] = digits[fingerprint[i] & 0x0f];
    }
    out[64] = '\0';
}

static bool json_fingerprint_from_hex(const char *text, size_t length,
                                      uint8_t out[32]) {
    if (length != 64) {
        return false;
    }
    for (size_t i = 0; i < 32; ++i) {
        int hi = json_hex_digit(text[i * 2]);
        int lo = json_hex_digit(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

/**
 * @brief Deterministic path digest used when no valid fingerprint is present.
 *
 * Mirrors `fallbackFingerprint` in kanariv, including its non-standard FNV
 * offset basis and the post-mix step. Changing any constant breaks import
 * compatibility with existing JSON exports.
 */
static void json_fallback_fingerprint(const cx_string *path, uint8_t out[32]) {
    uint64_t h = UINT64_C(1469598103934665603);
    cx_string_view view = json_str_view(path);
    for (size_t i = 0; i < view.length; ++i) {
        h ^= (uint8_t)view.data[i];
        h *= UINT64_C(1099511628211);
    }
    for (size_t i = 0; i < 32; ++i) {
        out[i] = (uint8_t)(h >> ((i % 8) * 8));
        h = h * UINT64_C(6364136223846793005) + 1;
    }
}

/* ------------------------------------------------------------------------- */
/* Source identity                                                           */
/* ------------------------------------------------------------------------- */

static cx_string_view json_source_value(const cx_string *value,
                                        ca_source_kind kind,
                                        const cx_string *fallback) {
    if (value && !cx_string_empty(value)) {
        return cx_string_view_of(value);
    }
    if (kind == CA_SOURCE_PATH && fallback) {
        return cx_string_view_of(fallback);
    }
    return json_empty_view();
}

static ca_status json_write_source(ca_json_writer *writer, const char *key,
                                   const ca_source_ref *ref,
                                   const cx_string *fallback, ca_error *err) {
    ca_source_kind kind = ref ? ref->kind : CA_SOURCE_PATH;
    const char *kind_name = ca_source_kind_name(kind);
    cx_string_view source_key =
        json_source_value(ref ? ref->key : nullptr, kind, fallback);
    cx_string_view display_name =
        json_source_value(ref ? ref->display_name : nullptr, kind, fallback);

    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(ca_json_writer_add_string(writer, "kind", kind_name,
                                          strlen(kind_name), err));
    CA_JSON_TRY(ca_json_writer_add_string(writer, "key", source_key.data,
                                          source_key.length, err));
    CA_JSON_TRY(ca_json_writer_add_string(writer, "display_name",
                                          display_name.data,
                                          display_name.length, err));
    CA_JSON_TRY(ca_json_writer_add_bool(writer, "available",
                                        ref ? ref->available : true, err));
    return ca_json_writer_end_object(writer, err);
}

/**
 * @brief Parse a source kind from a borrowed name.
 *
 * Uses the exact spellings of `sourceKindFromName` without requiring a
 * NUL-terminated copy.
 */
static bool json_source_kind_from_view(cx_string_view view,
                                       ca_source_kind *out) {
    if (cx_string_view_eq(view, cx_string_view_from_cstr("path"))) {
        *out = CA_SOURCE_PATH;
        return true;
    }
    if (cx_string_view_eq(view, cx_string_view_from_cstr("external"))) {
        *out = CA_SOURCE_EXTERNAL;
        return true;
    }
    if (cx_string_view_eq(view, cx_string_view_from_cstr("imported"))) {
        *out = CA_SOURCE_IMPORTED;
        return true;
    }
    if (cx_string_view_eq(view, cx_string_view_from_cstr("unavailable"))) {
        *out = CA_SOURCE_UNAVAILABLE;
        return true;
    }
    return false;
}

/**
 * @brief Read a source object into an initialized reference.
 *
 * @p fallback is the track path for `CA_SOURCE_PATH` defaults; nullptr for a
 * standalone source reference.
 */
static ca_status json_read_source(ca_source_ref *ref, const ca_json_val *obj,
                                  const cx_string *fallback, ca_error *err) {
    cx_string_view fallback_view = json_str_view(fallback);

    ref->kind = CA_SOURCE_PATH;
    ref->available = true;
    CA_JSON_TRY(json_assign_view(ref->key, fallback_view, err));
    CA_JSON_TRY(json_assign_view(ref->display_name, fallback_view, err));

    if (!json_is_object(obj)) {
        return CA_OK;
    }

    cx_string_view kind_view = json_empty_view();
    const ca_json_val *kind_val = ca_json_object_get(obj, "kind");
    if (kind_val && ca_json_type_of(kind_val) == CA_JSON_STRING) {
        size_t length = 0;
        const char *data = ca_json_get_string(kind_val, &length);
        kind_view = cx_string_view_from_parts(data ? data : "", length);
    } else {
        kind_view = cx_string_view_from_cstr("path");
    }

    ca_source_kind kind = CA_SOURCE_PATH;
    if (!json_source_kind_from_view(kind_view, &kind)) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "invalid source kind: %.*s",
                         (int)kind_view.length, kind_view.data);
        }
        return CA_INVALID_ARGUMENT;
    }
    ref->kind = kind;

    /* A present source object replaces the path defaults; missing members
     * clear rather than inherit them. */
    CA_JSON_TRY(json_read_str(obj, "key", ref->key, err));
    CA_JSON_TRY(json_read_str(obj, "display_name", ref->display_name, err));
    ref->available = json_bool_member(obj, "available", true);

    if (ref->kind == CA_SOURCE_PATH && cx_string_empty(ref->key)) {
        CA_JSON_TRY(json_assign_view(ref->key, fallback_view, err));
    }
    return CA_OK;
}

ca_status ca_source_ref_write_json(const ca_source_ref *ref,
                                   ca_json_writer *writer, const char *key,
                                   ca_error *err) {
    if (!writer || !ref) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT, "ref or writer is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }
    return json_write_source(writer, key, ref, nullptr, err);
}

ca_status ca_source_ref_from_json(ca_source_ref *ref, const ca_json_val *val,
                                  const cx_allocator *alloc, ca_error *err) {
    if (!ref) {
        return CA_INVALID_ARGUMENT;
    }
    memset(ref, 0, sizeof *ref);
    ca_status status = ca_source_ref_init(ref, alloc);
    if (status == CA_OK) {
        status = json_read_source(ref, val, nullptr, err);
    }
    if (status != CA_OK) {
        ca_source_ref_destroy(ref);
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/* Track                                                                     */
/* ------------------------------------------------------------------------- */

ca_status ca_track_write_json(const ca_track *track, ca_json_writer *writer,
                              const char *key, ca_error *err) {
    if (!track || !writer) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT,
                         "track or writer is nullptr");
        }
        return CA_INVALID_ARGUMENT;
    }

    char fingerprint_hex[65];
    json_fingerprint_to_hex(track->fingerprint, fingerprint_hex);

    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "id", track->id, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "size", track->size, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "mtime", track->mtime, err));
    CA_JSON_TRY(
        ca_json_writer_add_double(writer, "duration", track->duration, err));
    CA_JSON_TRY(
        ca_json_writer_add_uint(writer, "sample_rate", track->sample_rate, err));
    CA_JSON_TRY(
        ca_json_writer_add_uint(writer, "channels", track->channels, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "bitrate", track->bitrate, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "year", track->year, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "track_num", track->track_num, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "disc_num", track->disc_num, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "rating", track->rating, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "play_count", track->play_count, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "last_played", track->last_played, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "date_added", track->date_added, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "last_scanned",
                                       track->last_scanned, err));
    CA_JSON_TRY(ca_json_writer_add_bool(writer, "dirty", track->dirty, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "library_id", track->library_id, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "deleted_at", track->deleted_at, err));
    CA_JSON_TRY(ca_json_writer_add_string(writer, "fingerprint",
                                          fingerprint_hex,
                                          sizeof fingerprint_hex - 1, err));
    CA_JSON_TRY(json_add_str(writer, "path", track->path, err));
    CA_JSON_TRY(
        json_write_source(writer, "source", &track->source, track->path, err));
    CA_JSON_TRY(json_add_str(writer, "title", track->title, err));
    CA_JSON_TRY(json_add_str(writer, "artist", track->artist, err));
    CA_JSON_TRY(json_add_str(writer, "album", track->album, err));
    CA_JSON_TRY(json_add_str(writer, "album_artist", track->album_artist, err));
    CA_JSON_TRY(json_add_str(writer, "genre", track->genre, err));
    CA_JSON_TRY(
        json_add_str(writer, "cover_art_path", track->cover_art_path, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_track_from_json(ca_track *track, const ca_json_val *val,
                             const cx_allocator *alloc, ca_error *err) {
    if (!track || !json_is_object(val)) {
        if (err) {
            ca_error_set(err, CA_INVALID_ARGUMENT,
                         "track destination or JSON object is missing");
        }
        return CA_INVALID_ARGUMENT;
    }

    memset(track, 0, sizeof *track);
    ca_status status = ca_track_init(track, alloc);
    if (status != CA_OK) {
        return status;
    }

    track->id = json_int_member(val, "id", 0);
    track->size = json_int_member(val, "size", 0);
    track->mtime = json_int_member(val, "mtime", 0);
    track->duration = json_double_member(val, "duration", 0.0);
    track->sample_rate = (uint32_t)json_int_member(val, "sample_rate", 0);
    track->channels = (uint32_t)json_int_member(val, "channels", 0);
    track->bitrate = json_i32_member(val, "bitrate", 0);
    track->year = json_i32_member(val, "year", 0);
    track->track_num = json_i32_member(val, "track_num", 0);
    track->disc_num = json_i32_member(val, "disc_num", 0);
    track->rating = json_i32_member(val, "rating", 0);
    track->play_count = json_int_member(val, "play_count", 0);
    track->last_played = json_int_member(val, "last_played", 0);
    track->date_added = json_int_member(val, "date_added", 0);
    track->last_scanned = json_int_member(val, "last_scanned", 0);
    track->dirty = json_bool_member(val, "dirty", false) ||
                   json_int_member(val, "dirty", 0) != 0;
    track->library_id = json_int_member(val, "library_id", 1);
    if (track->library_id == 0) {
        track->library_id = 1;
    }
    track->deleted_at = json_int_member(val, "deleted_at", 0);

    status = json_read_str(val, "path", track->path, err);
    if (status == CA_OK) {
        status = json_read_source(&track->source,
                                  ca_json_object_get(val, "source"),
                                  track->path, err);
    }
    if (status == CA_OK) {
        status = json_read_str(val, "title", track->title, err);
    }
    if (status == CA_OK) {
        status = json_read_str(val, "artist", track->artist, err);
    }
    if (status == CA_OK) {
        status = json_read_str(val, "album", track->album, err);
    }
    if (status == CA_OK) {
        status = json_read_str(val, "album_artist", track->album_artist, err);
    }
    if (status == CA_OK) {
        status = json_read_str(val, "genre", track->genre, err);
    }
    if (status == CA_OK) {
        status = json_read_str(val, "cover_art_path", track->cover_art_path,
                               err);
    }
    if (status == CA_OK) {
        const ca_json_val *fp_value = ca_json_object_get(val, "fingerprint");
        bool decoded = false;
        if (fp_value && ca_json_type_of(fp_value) == CA_JSON_STRING) {
            size_t length = 0;
            const char *data = ca_json_get_string(fp_value, &length);
            if (data) {
                decoded =
                    json_fingerprint_from_hex(data, length, track->fingerprint);
            }
        }
        if (!decoded) {
            json_fallback_fingerprint(track->path, track->fingerprint);
        }
    }

    if (status != CA_OK) {
        ca_track_destroy(track);
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/* Playlist                                                                  */
/* ------------------------------------------------------------------------- */

ca_status ca_playlist_write_json(const ca_playlist *playlist,
                                 ca_json_writer *writer, const char *key,
                                 ca_error *err) {
    if (!playlist || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "id", playlist->id, err));
    CA_JSON_TRY(json_add_str(writer, "name", playlist->name, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "type", playlist->type, err));
    CA_JSON_TRY(
        json_add_str(writer, "smart_query", playlist->smart_query, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "created", playlist->created,
                                       err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "modified", playlist->modified, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "library_id", playlist->library_id, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_playlist_from_json(ca_playlist *playlist, const ca_json_val *val,
                                const cx_allocator *alloc, ca_error *err) {
    if (!playlist || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(playlist, 0, sizeof *playlist);
    ca_status status = ca_playlist_init(playlist, alloc);
    if (status != CA_OK) {
        return status;
    }

    playlist->id = json_int_member(val, "id", 0);
    playlist->type = json_i32_member(val, "type", 0);
    playlist->created = json_int_member(val, "created", 0);
    playlist->modified = json_int_member(val, "modified", 0);
    playlist->library_id = json_int_member(val, "library_id", 1);

    status = json_read_str(val, "name", playlist->name, err);
    if (status == CA_OK) {
        status = json_read_str(val, "smart_query", playlist->smart_query, err);
    }
    if (status != CA_OK) {
        ca_playlist_destroy(playlist);
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/* Queue item                                                                */
/* ------------------------------------------------------------------------- */

ca_status ca_queue_item_write_json(const ca_queue_item *item,
                                   ca_json_writer *writer, const char *key,
                                   ca_error *err) {
    if (!item || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "id", item->id, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "queue_id", item->queue_id, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "track_id", item->track_id, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "position", item->position, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "added", item->added, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_queue_item_from_json(ca_queue_item *item, const ca_json_val *val,
                                  const cx_allocator *alloc, ca_error *err) {
    (void)alloc;
    if (!item || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(item, 0, sizeof *item);
    item->queue_id = json_int_member(val, "queue_id", 1);
    item->id = json_int_member(val, "id", 0);
    item->track_id = json_int_member(val, "track_id", 0);
    item->position = json_int_member(val, "position", 0);
    item->added = json_int_member(val, "added", 0);
    (void)err;
    return CA_OK;
}

/* ------------------------------------------------------------------------- */
/* History entry                                                             */
/* ------------------------------------------------------------------------- */

ca_status ca_history_entry_write_json(const ca_history_entry *entry,
                                      ca_json_writer *writer, const char *key,
                                      ca_error *err) {
    if (!entry || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "id", entry->id, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "track_id", entry->track_id, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "started_at", entry->started_at, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "completed_at",
                                       entry->completed_at, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "position_ms", entry->position_ms, err));
    CA_JSON_TRY(ca_json_writer_add_double(writer, "completion_pct",
                                          entry->completion_pct, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "queue_id", entry->queue_id, err));
    CA_JSON_TRY(json_add_str(writer, "title", entry->title, err));
    CA_JSON_TRY(json_add_str(writer, "artist", entry->artist, err));
    CA_JSON_TRY(json_add_str(writer, "path", entry->path, err));
    CA_JSON_TRY(
        ca_json_writer_add_double(writer, "duration", entry->duration, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_history_entry_from_json(ca_history_entry *entry,
                                     const ca_json_val *val,
                                     const cx_allocator *alloc,
                                     ca_error *err) {
    if (!entry || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(entry, 0, sizeof *entry);
    ca_status status = ca_history_entry_init(entry, alloc);
    if (status != CA_OK) {
        return status;
    }

    entry->id = json_int_member(val, "id", 0);
    entry->track_id = json_int_member(val, "track_id", 0);
    entry->started_at = json_int_member(val, "started_at", 0);
    entry->completed_at = json_int_member(val, "completed_at", 0);
    entry->position_ms = json_int_member(val, "position_ms", 0);
    entry->completion_pct = json_double_member(val, "completion_pct", 0.0);
    entry->queue_id = json_int_member(val, "queue_id", 1);
    entry->duration = json_double_member(val, "duration", 0.0);

    status = json_read_str(val, "title", entry->title, err);
    if (status == CA_OK) {
        status = json_read_str(val, "artist", entry->artist, err);
    }
    if (status == CA_OK) {
        status = json_read_str(val, "path", entry->path, err);
    }
    if (status != CA_OK) {
        ca_history_entry_destroy(entry);
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/* Bookmark                                                                  */
/* ------------------------------------------------------------------------- */

ca_status ca_bookmark_write_json(const ca_bookmark *bookmark,
                                 ca_json_writer *writer, const char *key,
                                 ca_error *err) {
    if (!bookmark || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "id", bookmark->id, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "track_id", bookmark->track_id, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "position_ms",
                                       bookmark->position_ms, err));
    CA_JSON_TRY(json_add_str(writer, "note", bookmark->note, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "created", bookmark->created, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_bookmark_from_json(ca_bookmark *bookmark, const ca_json_val *val,
                                const cx_allocator *alloc, ca_error *err) {
    if (!bookmark || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(bookmark, 0, sizeof *bookmark);
    ca_status status = ca_bookmark_init(bookmark, alloc);
    if (status != CA_OK) {
        return status;
    }

    bookmark->id = json_int_member(val, "id", 0);
    bookmark->track_id = json_int_member(val, "track_id", 0);
    bookmark->position_ms = json_int_member(val, "position_ms", 0);
    bookmark->created = json_int_member(val, "created", 0);

    status = json_read_str(val, "note", bookmark->note, err);
    if (status != CA_OK) {
        ca_bookmark_destroy(bookmark);
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/* Device info / list                                                        */
/* ------------------------------------------------------------------------- */

ca_status ca_device_info_write_json(const ca_device_info *device,
                                    ca_json_writer *writer, const char *key,
                                    ca_error *err) {
    if (!device || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(json_add_str(writer, "id", device->id, err));
    CA_JSON_TRY(json_add_str(writer, "name", device->name, err));
    CA_JSON_TRY(
        ca_json_writer_add_bool(writer, "isDefault", device->is_default, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_device_info_from_json(ca_device_info *device,
                                   const ca_json_val *val,
                                   const cx_allocator *alloc, ca_error *err) {
    if (!device || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(device, 0, sizeof *device);
    ca_status status = ca_device_info_init(device, alloc);
    if (status != CA_OK) {
        return status;
    }

    device->is_default = json_bool_member(val, "isDefault", false);
    status = json_read_str(val, "id", device->id, err);
    if (status == CA_OK) {
        status = json_read_str(val, "name", device->name, err);
    }
    if (status != CA_OK) {
        ca_device_info_destroy(device);
    }
    return status;
}

static ca_status json_read_device_list(cx_vector **out, const ca_json_val *arr,
                                       const cx_allocator *alloc,
                                       ca_error *err) {
    cx_status cx = cx_vector_init(out, sizeof(ca_device_info),
                                  alignof(ca_device_info),
                                  &ca_impl_device_info_vector_ops, alloc);
    if (cx != CX_OK) {
        return json_from_cx(cx, err);
    }

    ca_impl_copy_reset(alloc);
    size_t count = ca_json_array_size(arr);
    for (size_t i = 0; i < count; ++i) {
        ca_device_info element;
        ca_status status =
            ca_device_info_from_json(&element, ca_json_array_get(arr, i),
                                     alloc, err);
        if (status != CA_OK) {
            cx_vector_destroy(out);
            return status;
        }
        cx = cx_vector_push_copy(*out, &element, sizeof element);
        ca_device_info_destroy(&element);
        if (cx != CX_OK) {
            cx_vector_destroy(out);
            return json_from_cx(cx, err);
        }
        if (ca_impl_copy_status != CA_OK) {
            cx_vector_destroy(out);
            return ca_impl_copy_status;
        }
    }
    return CA_OK;
}

ca_status ca_device_list_write_json(const ca_device_list *list,
                                    ca_json_writer *writer, const char *key,
                                    ca_error *err) {
    if (!list || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(ca_json_writer_begin_array(writer, "devices", err));
    size_t count = list->devices ? cx_vector_length(list->devices) : 0;
    for (size_t i = 0; i < count; ++i) {
        const ca_device_info *device =
            (const ca_device_info *)cx_vector_ptr(list->devices, i);
        CA_JSON_TRY(ca_device_info_write_json(device, writer, nullptr, err));
    }
    CA_JSON_TRY(ca_json_writer_end_array(writer, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_device_list_from_json(ca_device_list *list,
                                   const ca_json_val *val,
                                   const cx_allocator *alloc, ca_error *err) {
    if (!list || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(list, 0, sizeof *list);
    ca_status status = ca_device_list_init(list, alloc);
    if (status != CA_OK) {
        return status;
    }

    const ca_json_val *devices = ca_json_object_get(val, "devices");
    if (json_is_array(devices)) {
        cx_vector_destroy(&list->devices);
        status = json_read_device_list(&list->devices, devices, alloc, err);
    }
    if (status != CA_OK) {
        ca_device_list_destroy(list);
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/* Track metadata                                                            */
/* ------------------------------------------------------------------------- */

ca_status ca_track_metadata_write_json(const ca_track_metadata *metadata,
                                       ca_json_writer *writer,
                                       const char *key, ca_error *err) {
    if (!metadata || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(json_add_str(writer, "title", metadata->title, err));
    CA_JSON_TRY(json_add_str(writer, "artist", metadata->artist, err));
    CA_JSON_TRY(json_add_str(writer, "album", metadata->album, err));
    CA_JSON_TRY(
        json_add_str(writer, "album_artist", metadata->album_artist, err));
    CA_JSON_TRY(json_add_str(writer, "genre", metadata->genre, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "year", metadata->year, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "track_num", metadata->track_num, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "disc_num", metadata->disc_num, err));
    CA_JSON_TRY(
        ca_json_writer_add_double(writer, "duration", metadata->duration, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "sample_rate", metadata->sample_rate,
                               err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "channels", metadata->channels, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "bitrate", metadata->bitrate, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_track_metadata_from_json(ca_track_metadata *metadata,
                                      const ca_json_val *val,
                                      const cx_allocator *alloc,
                                      ca_error *err) {
    if (!metadata || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(metadata, 0, sizeof *metadata);
    ca_status status =
        ca_track_metadata_init(metadata, alloc);
    if (status != CA_OK) {
        return status;
    }

    metadata->year = (int)json_int_member(val, "year", 0);
    metadata->track_num = (int)json_int_member(val, "track_num", 0);
    metadata->disc_num = (int)json_int_member(val, "disc_num", 0);
    metadata->duration = json_double_member(val, "duration", 0.0);
    metadata->sample_rate = (int)json_int_member(val, "sample_rate", 0);
    metadata->channels = (int)json_int_member(val, "channels", 0);
    metadata->bitrate = (int)json_int_member(val, "bitrate", 0);

    status = json_read_str(val, "title", metadata->title, err);
    if (status == CA_OK) {
        status = json_read_str(val, "artist", metadata->artist, err);
    }
    if (status == CA_OK) {
        status = json_read_str(val, "album", metadata->album, err);
    }
    if (status == CA_OK) {
        status =
            json_read_str(val, "album_artist", metadata->album_artist, err);
    }
    if (status == CA_OK) {
        status = json_read_str(val, "genre", metadata->genre, err);
    }
    if (status != CA_OK) {
        ca_track_metadata_destroy(metadata);
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/* Top entity                                                                */
/* ------------------------------------------------------------------------- */

ca_status ca_top_entity_write_json(const ca_top_entity *entity,
                                   ca_json_writer *writer, const char *key,
                                   ca_error *err) {
    if (!entity || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(json_add_str(writer, "name", entity->name, err));
    CA_JSON_TRY(ca_json_writer_add_uint(writer, "track_count",
                                        (uint64_t)entity->track_count, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "total_plays", entity->total_plays, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_top_entity_from_json(ca_top_entity *entity,
                                  const ca_json_val *val,
                                  const cx_allocator *alloc, ca_error *err) {
    if (!entity || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(entity, 0, sizeof *entity);
    ca_status status = ca_top_entity_init(entity, alloc);
    if (status != CA_OK) {
        return status;
    }

    int64_t track_count = json_int_member(val, "track_count", 0);
    entity->track_count = track_count < 0 ? 0 : (size_t)track_count;
    entity->total_plays = json_int_member(val, "total_plays", 0);

    status = json_read_str(val, "name", entity->name, err);
    if (status != CA_OK) {
        ca_top_entity_destroy(entity);
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/* Library stats                                                             */
/* ------------------------------------------------------------------------- */

static ca_status json_read_track_list(cx_vector **out, const ca_json_val *arr,
                                      const cx_allocator *alloc,
                                      ca_error *err) {
    cx_status cx = cx_vector_init(out, sizeof(ca_track), alignof(ca_track),
                                  &ca_impl_track_vector_ops, alloc);
    if (cx != CX_OK) {
        return json_from_cx(cx, err);
    }

    ca_impl_copy_reset(alloc);
    size_t count = ca_json_array_size(arr);
    for (size_t i = 0; i < count; ++i) {
        ca_track element;
        ca_status status =
            ca_track_from_json(&element, ca_json_array_get(arr, i), alloc, err);
        if (status != CA_OK) {
            cx_vector_destroy(out);
            return status;
        }
        cx = cx_vector_push_copy(*out, &element, sizeof element);
        ca_track_destroy(&element);
        if (cx != CX_OK) {
            cx_vector_destroy(out);
            return json_from_cx(cx, err);
        }
        if (ca_impl_copy_status != CA_OK) {
            cx_vector_destroy(out);
            return ca_impl_copy_status;
        }
    }
    return CA_OK;
}

static ca_status json_read_top_list(cx_vector **out, const ca_json_val *arr,
                                    const cx_allocator *alloc, ca_error *err) {
    cx_status cx = cx_vector_init(out, sizeof(ca_top_entity),
                                  alignof(ca_top_entity),
                                  &ca_impl_top_entity_vector_ops, alloc);
    if (cx != CX_OK) {
        return json_from_cx(cx, err);
    }

    ca_impl_copy_reset(alloc);
    size_t count = ca_json_array_size(arr);
    for (size_t i = 0; i < count; ++i) {
        ca_top_entity element;
        ca_status status =
            ca_top_entity_from_json(&element, ca_json_array_get(arr, i),
                                    alloc, err);
        if (status != CA_OK) {
            cx_vector_destroy(out);
            return status;
        }
        cx = cx_vector_push_copy(*out, &element, sizeof element);
        ca_top_entity_destroy(&element);
        if (cx != CX_OK) {
            cx_vector_destroy(out);
            return json_from_cx(cx, err);
        }
        if (ca_impl_copy_status != CA_OK) {
            cx_vector_destroy(out);
            return ca_impl_copy_status;
        }
    }
    return CA_OK;
}

static ca_status json_write_top_list(ca_json_writer *writer, const char *key,
                                     const cx_vector *list, ca_error *err) {
    CA_JSON_TRY(ca_json_writer_begin_array(writer, key, err));
    size_t count = list ? cx_vector_length(list) : 0;
    for (size_t i = 0; i < count; ++i) {
        const ca_top_entity *entity =
            (const ca_top_entity *)cx_vector_ptr(list, i);
        CA_JSON_TRY(ca_top_entity_write_json(entity, writer, nullptr, err));
    }
    return ca_json_writer_end_array(writer, err);
}

static ca_status json_write_track_list(ca_json_writer *writer, const char *key,
                                       const cx_vector *list, ca_error *err) {
    CA_JSON_TRY(ca_json_writer_begin_array(writer, key, err));
    size_t count = list ? cx_vector_length(list) : 0;
    for (size_t i = 0; i < count; ++i) {
        const ca_track *track = (const ca_track *)cx_vector_ptr(list, i);
        CA_JSON_TRY(ca_track_write_json(track, writer, nullptr, err));
    }
    return ca_json_writer_end_array(writer, err);
}

ca_status ca_library_stats_write_json(const ca_library_stats *stats,
                                      ca_json_writer *writer, const char *key,
                                      ca_error *err) {
    if (!stats || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(ca_json_writer_add_uint(writer, "tracks",
                                        (uint64_t)stats->tracks, err));
    CA_JSON_TRY(ca_json_writer_add_uint(writer, "queues",
                                        (uint64_t)stats->queues, err));
    CA_JSON_TRY(ca_json_writer_add_uint(writer, "playlists",
                                        (uint64_t)stats->playlists, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "total_duration_ms",
                                       stats->total_duration_ms, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "total_play_time_ms",
                                       stats->total_play_time_ms, err));
    if (stats->has_top_tracks) {
        CA_JSON_TRY(
            json_write_track_list(writer, "top_tracks", stats->top_tracks, err));
    }
    if (stats->has_top_albums) {
        CA_JSON_TRY(
            json_write_top_list(writer, "top_albums", stats->top_albums, err));
    }
    if (stats->has_top_genres) {
        CA_JSON_TRY(
            json_write_top_list(writer, "top_genres", stats->top_genres, err));
    }
    if (stats->has_top_artists) {
        CA_JSON_TRY(
            json_write_top_list(writer, "top_artists", stats->top_artists, err));
    }
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_library_stats_from_json(ca_library_stats *stats,
                                     const ca_json_val *val,
                                     const cx_allocator *alloc,
                                     ca_error *err) {
    if (!stats || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(stats, 0, sizeof *stats);
    ca_status status = ca_library_stats_init(stats, alloc);
    if (status != CA_OK) {
        return status;
    }

    int64_t tracks = json_int_member(val, "tracks", 0);
    int64_t queues = json_int_member(val, "queues", 0);
    int64_t playlists = json_int_member(val, "playlists", 0);
    stats->tracks = tracks < 0 ? 0 : (size_t)tracks;
    stats->queues = queues < 0 ? 0 : (size_t)queues;
    stats->playlists = playlists < 0 ? 0 : (size_t)playlists;
    stats->total_duration_ms = json_int_member(val, "total_duration_ms", 0);
    stats->total_play_time_ms = json_int_member(val, "total_play_time_ms", 0);

    const ca_json_val *top = ca_json_object_get(val, "top_tracks");
    if (json_is_array(top)) {
        stats->has_top_tracks = true;
        status = json_read_track_list(&stats->top_tracks, top, alloc, err);
    }
    if (status == CA_OK) {
        top = ca_json_object_get(val, "top_albums");
        if (json_is_array(top)) {
            stats->has_top_albums = true;
            status =
                json_read_top_list(&stats->top_albums, top, alloc, err);
        }
    }
    if (status == CA_OK) {
        top = ca_json_object_get(val, "top_genres");
        if (json_is_array(top)) {
            stats->has_top_genres = true;
            status =
                json_read_top_list(&stats->top_genres, top, alloc, err);
        }
    }
    if (status == CA_OK) {
        top = ca_json_object_get(val, "top_artists");
        if (json_is_array(top)) {
            stats->has_top_artists = true;
            status =
                json_read_top_list(&stats->top_artists, top, alloc, err);
        }
    }
    if (status != CA_OK) {
        ca_library_stats_destroy(stats);
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/* Queue                                                                     */
/* ------------------------------------------------------------------------- */

ca_status ca_queue_write_json(const ca_queue *queue, ca_json_writer *writer,
                              const char *key, ca_error *err) {
    if (!queue || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "id", queue->id, err));
    CA_JSON_TRY(json_add_str(writer, "name", queue->name, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "repeat_mode", queue->repeat_mode, err));
    CA_JSON_TRY(ca_json_writer_add_bool(writer, "temp", queue->temp, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "library_id", queue->library_id, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_queue_from_json(ca_queue *queue, const ca_json_val *val,
                             const cx_allocator *alloc, ca_error *err) {
    if (!queue || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(queue, 0, sizeof *queue);
    ca_status status = ca_queue_init(queue, alloc);
    if (status != CA_OK) {
        return status;
    }

    queue->id = json_int_member(val, "id", 0);
    queue->repeat_mode = json_i32_member(val, "repeat_mode", 0);
    queue->temp = json_bool_member(val, "temp", false);
    queue->library_id = json_int_member(val, "library_id", 1);

    status = json_read_str(val, "name", queue->name, err);
    if (status != CA_OK) {
        ca_queue_destroy(queue);
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/* Library                                                                   */
/* ------------------------------------------------------------------------- */

ca_status ca_library_write_json(const ca_library *library,
                                ca_json_writer *writer, const char *key,
                                ca_error *err) {
    if (!library || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "id", library->id, err));
    CA_JSON_TRY(json_add_str(writer, "path", library->path, err));
    CA_JSON_TRY(json_add_str(writer, "name", library->name, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "date_added",
                                       library->date_added, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "last_scanned",
                                       library->last_scanned, err));
    CA_JSON_TRY(
        ca_json_writer_add_bool(writer, "auto_scan", library->auto_scan, err));
    CA_JSON_TRY(
        ca_json_writer_add_bool(writer, "recursive", library->recursive, err));
    CA_JSON_TRY(
        json_add_str(writer, "extensions", library->extensions, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_library_from_json(ca_library *library, const ca_json_val *val,
                               const cx_allocator *alloc, ca_error *err) {
    if (!library || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(library, 0, sizeof *library);
    ca_status status = ca_library_init(library, alloc);
    if (status != CA_OK) {
        return status;
    }

    library->id = json_int_member(val, "id", 0);
    library->date_added = json_int_member(val, "date_added", 0);
    library->last_scanned = json_int_member(val, "last_scanned", 0);
    library->auto_scan = json_bool_member(val, "auto_scan", true);
    library->recursive = json_bool_member(val, "recursive", true);

    status = json_read_str(val, "path", library->path, err);
    if (status == CA_OK) {
        status = json_read_str(val, "name", library->name, err);
    }
    if (status == CA_OK) {
        status = json_read_str(val, "extensions", library->extensions, err);
    }
    if (status != CA_OK) {
        ca_library_destroy(library);
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/* Playback statistics                                                       */
/* ------------------------------------------------------------------------- */

ca_status ca_playback_stats_write_json(const ca_playback_stats *stats,
                                       ca_json_writer *writer,
                                       const char *key, ca_error *err) {
    if (!stats || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "num_tracks", stats->num_tracks, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "num_playlists",
                                       stats->num_playlists, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "num_queue_items",
                                       stats->num_queue_items, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "num_history", stats->num_history, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "num_bookmarks",
                                       stats->num_bookmarks, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "num_libraries",
                                       stats->num_libraries, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "total_duration_ms",
                                       stats->total_duration_ms, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_playback_stats_from_json(ca_playback_stats *stats,
                                      const ca_json_val *val,
                                      const cx_allocator *alloc,
                                      ca_error *err) {
    (void)alloc;
    if (!stats || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(stats, 0, sizeof *stats);
    stats->num_tracks = json_int_member(val, "num_tracks", 0);
    stats->num_playlists = json_int_member(val, "num_playlists", 0);
    stats->num_queue_items = json_int_member(val, "num_queue_items", 0);
    stats->num_history = json_int_member(val, "num_history", 0);
    stats->num_bookmarks = json_int_member(val, "num_bookmarks", 0);
    stats->num_libraries = json_int_member(val, "num_libraries", 0);
    stats->total_duration_ms = json_int_member(val, "total_duration_ms", 0);
    (void)err;
    return CA_OK;
}

/* ------------------------------------------------------------------------- */
/* Track query                                                               */
/* ------------------------------------------------------------------------- */

static ca_status json_write_optional_int(ca_json_writer *writer,
                                         const char *key, bool has,
                                         int64_t value, ca_error *err) {
    if (!has) {
        return ca_json_writer_add_null(writer, key, err);
    }
    return ca_json_writer_add_int(writer, key, value, err);
}

static ca_status json_write_optional_double(ca_json_writer *writer,
                                            const char *key, bool has,
                                            double value, ca_error *err) {
    if (!has) {
        return ca_json_writer_add_null(writer, key, err);
    }
    return ca_json_writer_add_double(writer, key, value, err);
}

static ca_status json_write_optional_bool(ca_json_writer *writer,
                                          const char *key, bool has,
                                          bool value, ca_error *err) {
    if (!has) {
        return ca_json_writer_add_null(writer, key, err);
    }
    return ca_json_writer_add_bool(writer, key, value, err);
}

ca_status ca_track_query_write_json(const ca_track_query *query,
                                    ca_json_writer *writer, const char *key,
                                    ca_error *err) {
    if (!query || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(json_add_str(writer, "artist", query->artist, err));
    CA_JSON_TRY(json_add_str(writer, "album", query->album, err));
    CA_JSON_TRY(json_add_str(writer, "genre", query->genre, err));
    CA_JSON_TRY(json_write_optional_int(writer, "year", query->has_year,
                                        query->year, err));
    CA_JSON_TRY(json_write_optional_int(writer, "year_min",
                                        query->has_year_min, query->year_min,
                                        err));
    CA_JSON_TRY(json_write_optional_int(writer, "year_max",
                                        query->has_year_max, query->year_max,
                                        err));
    CA_JSON_TRY(json_write_optional_int(writer, "library_id",
                                        query->has_library_id,
                                        query->library_id, err));
    CA_JSON_TRY(json_write_optional_bool(writer, "dirty", query->has_dirty,
                                         query->dirty, err));
    CA_JSON_TRY(json_write_optional_int(writer, "rating", query->has_rating,
                                        query->rating, err));
    CA_JSON_TRY(json_write_optional_int(writer, "play_count_min",
                                        query->has_play_count_min,
                                        query->play_count_min, err));
    CA_JSON_TRY(json_write_optional_int(writer, "play_count_max",
                                        query->has_play_count_max,
                                        query->play_count_max, err));
    CA_JSON_TRY(json_write_optional_double(writer, "duration_min",
                                           query->has_duration_min,
                                           query->duration_min, err));
    CA_JSON_TRY(json_write_optional_double(writer, "duration_max",
                                           query->has_duration_max,
                                           query->duration_max, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "limit", query->limit, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "offset", query->offset, err));
    CA_JSON_TRY(json_add_str(writer, "search", query->search, err));
    CA_JSON_TRY(json_add_str(writer, "sort", query->sort, err));
    CA_JSON_TRY(json_add_str(writer, "order", query->order, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_track_query_from_json(ca_track_query *query,
                                   const ca_json_val *val,
                                   const cx_allocator *alloc, ca_error *err) {
    if (!query || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(query, 0, sizeof *query);
    ca_status status = ca_track_query_init(query, alloc);
    if (status != CA_OK) {
        return status;
    }

    int64_t value = 0;
    json_read_optional_int(val, "year", &query->has_year, &value);
    query->year = (int32_t)value;
    json_read_optional_int(val, "year_min", &query->has_year_min, &value);
    query->year_min = (int32_t)value;
    json_read_optional_int(val, "year_max", &query->has_year_max, &value);
    query->year_max = (int32_t)value;
    json_read_optional_int(val, "library_id", &query->has_library_id, &value);
    query->library_id = value;
    json_read_optional_bool(val, "dirty", &query->has_dirty, &query->dirty);
    json_read_optional_int(val, "rating", &query->has_rating, &value);
    query->rating = (int32_t)value;
    json_read_optional_int(val, "play_count_min", &query->has_play_count_min,
                           &query->play_count_min);
    json_read_optional_int(val, "play_count_max", &query->has_play_count_max,
                           &query->play_count_max);

    /* Durations are doubles so fractional seconds survive. */
    const ca_json_val *dur = ca_json_object_get(val, "duration_min");
    double duration = 0.0;
    if (json_number_double(dur, &duration)) {
        query->has_duration_min = true;
        query->duration_min = duration;
    } else {
        query->has_duration_min = false;
        query->duration_min = 0.0;
    }
    dur = ca_json_object_get(val, "duration_max");
    if (json_number_double(dur, &duration)) {
        query->has_duration_max = true;
        query->duration_max = duration;
    } else {
        query->has_duration_max = false;
        query->duration_max = 0.0;
    }

    query->limit = (int)json_int_member(val, "limit", 0);
    query->offset = (int)json_int_member(val, "offset", 0);

    status = json_read_str(val, "artist", query->artist, err);
    if (status == CA_OK) {
        status = json_read_str(val, "album", query->album, err);
    }
    if (status == CA_OK) {
        status = json_read_str(val, "genre", query->genre, err);
    }
    if (status == CA_OK) {
        status = json_read_str(val, "search", query->search, err);
    }
    if (status == CA_OK) {
        status = json_read_str(val, "sort", query->sort, err);
    }
    if (status == CA_OK) {
        status = json_read_str(val, "order", query->order, err);
    }
    if (status != CA_OK) {
        ca_track_query_destroy(query);
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/* History query                                                             */
/* ------------------------------------------------------------------------- */

ca_status ca_history_query_write_json(const ca_history_query *query,
                                      ca_json_writer *writer, const char *key,
                                      ca_error *err) {
    if (!query || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(json_write_optional_int(writer, "track_id",
                                        query->has_track_id, query->track_id,
                                        err));
    CA_JSON_TRY(json_write_optional_int(writer, "queue_id",
                                        query->has_queue_id, query->queue_id,
                                        err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "limit", query->limit, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "offset", query->offset, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_history_query_from_json(ca_history_query *query,
                                     const ca_json_val *val,
                                     const cx_allocator *alloc,
                                     ca_error *err) {
    (void)alloc;
    if (!query || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(query, 0, sizeof *query);
    json_read_optional_int(val, "track_id", &query->has_track_id,
                           &query->track_id);
    json_read_optional_int(val, "queue_id", &query->has_queue_id,
                           &query->queue_id);
    query->limit = (int)json_int_member(val, "limit", 0);
    query->offset = (int)json_int_member(val, "offset", 0);
    (void)err;
    return CA_OK;
}

/* ------------------------------------------------------------------------- */
/* Engine event                                                              */
/* ------------------------------------------------------------------------- */

ca_status ca_engine_event_write_json(const ca_engine_event *event,
                                     ca_json_writer *writer, const char *key,
                                     ca_error *err) {
    if (!event || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    const char *type_name = ca_engine_event_type_name(event->type);
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(ca_json_writer_add_string(writer, "type", type_name,
                                          strlen(type_name), err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "track_id", event->track_id, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "queue_id", event->queue_id, err));
    CA_JSON_TRY(
        ca_json_writer_add_double(writer, "position", event->position, err));
    CA_JSON_TRY(
        ca_json_writer_add_double(writer, "duration", event->duration, err));
    CA_JSON_TRY(json_add_str(writer, "msg", event->msg, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_engine_event_from_json(ca_engine_event *event,
                                    const ca_json_val *val,
                                    const cx_allocator *alloc,
                                    ca_error *err) {
    if (!event || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(event, 0, sizeof *event);
    ca_status status = ca_engine_event_init(event, alloc);
    if (status != CA_OK) {
        return status;
    }

    const ca_json_val *type_val = ca_json_object_get(val, "type");
    if (type_val && ca_json_type_of(type_val) == CA_JSON_STRING) {
        size_t length = 0;
        const char *data = ca_json_get_string(type_val, &length);
        cx_string_view view = cx_string_view_from_parts(data ? data : "", length);
        if (cx_string_view_eq(view, cx_string_view_from_cstr("None"))) {
            event->type = CA_ENGINE_EVENT_NONE;
        } else if (cx_string_view_eq(
                       view, cx_string_view_from_cstr("TrackStarted"))) {
            event->type = CA_ENGINE_EVENT_TRACK_STARTED;
        } else if (cx_string_view_eq(
                       view, cx_string_view_from_cstr("TrackEnded"))) {
            event->type = CA_ENGINE_EVENT_TRACK_ENDED;
        } else if (cx_string_view_eq(
                       view, cx_string_view_from_cstr("QueueChanged"))) {
            event->type = CA_ENGINE_EVENT_QUEUE_CHANGED;
        } else if (cx_string_view_eq(
                       view, cx_string_view_from_cstr("Progress"))) {
            event->type = CA_ENGINE_EVENT_PROGRESS;
        } else if (cx_string_view_eq(
                       view, cx_string_view_from_cstr("Error"))) {
            event->type = CA_ENGINE_EVENT_ERROR;
        }
    }

    event->track_id = json_int_member(val, "track_id", 0);
    event->queue_id = json_int_member(val, "queue_id", 1);
    event->position = json_double_member(val, "position", 0.0);
    event->duration = json_double_member(val, "duration", 0.0);

    status = json_read_str(val, "msg", event->msg, err);
    if (status != CA_OK) {
        ca_engine_event_destroy(event);
    }
    return status;
}

/* ------------------------------------------------------------------------- */
/* Engine state                                                              */
/* ------------------------------------------------------------------------- */

ca_status ca_engine_state_write_json(const ca_engine_state *state,
                                     ca_json_writer *writer, const char *key,
                                     ca_error *err) {
    if (!state || !writer) {
        return CA_INVALID_ARGUMENT;
    }
    CA_JSON_TRY(ca_json_writer_begin_object(writer, key, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "shuffle_enabled",
                               state->shuffle_enabled ? 1 : 0, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "repeat_mode", state->repeat_mode, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "cursor_pos", state->cursor_pos,
                                       err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "current_track_id",
                                       state->current_track_id, err));
    CA_JSON_TRY(ca_json_writer_add_double(writer, "volume",
                                          (double)state->volume, err));
    CA_JSON_TRY(ca_json_writer_add_int(writer, "active_queue_id",
                                       state->active_queue_id, err));
    CA_JSON_TRY(ca_json_writer_add_double(writer, "pre_mute_volume",
                                          (double)state->pre_mute_volume, err));
    CA_JSON_TRY(
        ca_json_writer_add_int(writer, "position_ms", state->position_ms, err));
    CA_JSON_TRY(ca_json_writer_begin_array(writer, "shuffle_perm", err));
    size_t count = state->shuffle_perm ? cx_vector_length(state->shuffle_perm)
                                       : 0;
    for (size_t i = 0; i < count; ++i) {
        const int64_t *value =
            (const int64_t *)cx_vector_ptr(state->shuffle_perm, i);
        CA_JSON_TRY(ca_json_writer_add_int(writer, nullptr, *value, err));
    }
    CA_JSON_TRY(ca_json_writer_end_array(writer, err));
    return ca_json_writer_end_object(writer, err);
}

ca_status ca_engine_state_from_json(ca_engine_state *state,
                                    const ca_json_val *val,
                                    const cx_allocator *alloc,
                                    ca_error *err) {
    if (!state || !json_is_object(val)) {
        return CA_INVALID_ARGUMENT;
    }
    memset(state, 0, sizeof *state);
    ca_status status = ca_engine_state_init(state, alloc);
    if (status != CA_OK) {
        return status;
    }

    state->shuffle_enabled =
        json_int_member(val, "shuffle_enabled", 0) != 0 ? 1 : 0;
    state->repeat_mode =
        (ca_repeat_mode)json_i32_member(val, "repeat_mode", 0);
    state->cursor_pos = json_int_member(val, "cursor_pos", 0);
    state->current_track_id = json_int_member(val, "current_track_id", 0);
    state->volume = (float)json_double_member(val, "volume", 1.0);
    state->active_queue_id = json_int_member(val, "active_queue_id", 1);
    state->pre_mute_volume =
        (float)json_double_member(val, "pre_mute_volume", 0.5);
    state->position_ms = json_int_member(val, "position_ms", 0);

    const ca_json_val *perm = ca_json_object_get(val, "shuffle_perm");
    if (json_is_array(perm)) {
        size_t count = ca_json_array_size(perm);
        for (size_t i = 0; i < count; ++i) {
            int64_t value = 0;
            (void)json_number_int(ca_json_array_get(perm, i), &value);
            cx_status cx = cx_vector_push_copy(state->shuffle_perm, &value,
                                               sizeof value);
            if (cx != CX_OK) {
                ca_engine_state_destroy(state);
                return json_from_cx(cx, err);
            }
        }
    }

    return CA_OK;
}

#undef CA_JSON_TRY
