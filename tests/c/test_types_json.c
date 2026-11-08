#include <caudio/runtime/json.h>
#include <caudio/types/json.h>
#include <caudio/types/record.h>

#include <cstdx/allocator_counted.h>
#include <cstdx/string.h>
#include <cstdx/string_view.h>

#include <string.h>

#include "ca_test.h"

static cx_allocator_counted g_counted;
static cx_allocator g_alloc;

static bool view_eq(const cx_string *left, const cx_string *right) {
    if (!left || !right) {
        return false;
    }
    return cx_string_view_eq(cx_string_view_of(left), cx_string_view_of(right));
}

static ca_json *parse_text(const char *text) {
    ca_json *doc = NULL;
    if (ca_json_parse(&doc, text, 0, &g_alloc, NULL) != CA_OK) {
        return NULL;
    }
    return doc;
}

static ca_json *parse_string(const cx_string *text) {
    ca_json *doc = NULL;
    if (ca_json_parse(&doc, cx_string_cstr(text), cx_string_length(text),
                      &g_alloc, NULL) != CA_OK) {
        return NULL;
    }
    return doc;
}

/*
 * Serialize @p write, parse the text, decode into a fresh record, serialize
 * that record, and require both documents to be byte-identical. This is the
 * round-trip gate for every record type.
 */
#define ROUNDTRIP(TYPE, write_first, read_decoded, write_second,               \
                  destroy_decoded)                                             \
    do {                                                                       \
        ca_json_writer *w1 = NULL;                                             \
        ca_json_writer *w2 = NULL;                                             \
        cx_string *first = NULL;                                               \
        cx_string *second = NULL;                                              \
        ca_json *doc = NULL;                                                   \
        TYPE decoded;                                                          \
        memset(&decoded, 0, sizeof decoded);                                   \
        CA_CHECK_EQ(CA_OK, ca_json_writer_init(&w1, 0, &g_alloc, NULL));       \
        CA_CHECK_EQ(CA_OK, write_first);                                       \
        CA_CHECK_EQ(CA_OK, ca_json_writer_finish(w1, &first, NULL));           \
        ca_json_writer_destroy(&w1);                                           \
        if (first) {                                                           \
            doc = parse_string(first);                                         \
        }                                                                      \
        CA_CHECK(doc != NULL);                                                 \
        if (doc) {                                                             \
            const ca_json_val *root = ca_json_root(doc);                       \
            CA_CHECK_EQ(CA_OK, read_decoded);                                  \
            CA_CHECK_EQ(CA_OK, ca_json_writer_init(&w2, 0, &g_alloc, NULL));   \
            CA_CHECK_EQ(CA_OK, write_second);                                  \
            CA_CHECK_EQ(CA_OK, ca_json_writer_finish(w2, &second, NULL));      \
            ca_json_writer_destroy(&w2);                                       \
            CA_CHECK(view_eq(first, second));                                  \
        }                                                                      \
        destroy_decoded;                                                       \
        ca_json_destroy(&doc);                                                 \
        cx_string_destroy(&first);                                             \
        cx_string_destroy(&second);                                            \
    } while (0)

static void test_track_json(void) {
    ca_track track;
    CA_CHECK_EQ(CA_OK, ca_track_init(&track, &g_alloc));
    track.id = 7;
    track.size = 1234;
    track.mtime = 1700000000;
    track.duration = 3.5;
    track.sample_rate = 44100;
    track.channels = 2;
    track.bitrate = 320000;
    track.year = 2020;
    track.track_num = 3;
    track.disc_num = 1;
    track.rating = 5;
    track.play_count = 11;
    track.last_played = 1700000100;
    track.date_added = 1700000200;
    track.last_scanned = 1700000300;
    track.dirty = true;
    track.library_id = 2;
    track.deleted_at = 0;
    for (uint8_t i = 0; i < 32; ++i) {
        track.fingerprint[i] = i;
    }
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.path, "/music/a.mp3"));
    track.source.kind = CA_SOURCE_EXTERNAL;
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.source.key, "content://x"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.source.display_name, "Song"));
    track.source.available = false;
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.title, "Song"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.artist, "Artist"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.album, "Album"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.album_artist, "Album Artist"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.genre, "Rock"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.cover_art_path, "/art.jpg"));

    /* Golden field order and spellings. */
    ROUNDTRIP(ca_track, ca_track_write_json(&track, w1, NULL, NULL),
              ca_track_from_json(&decoded, root, &g_alloc, NULL),
              ca_track_write_json(&decoded, w2, NULL, NULL),
              ca_track_destroy(&decoded));

    ca_json_writer *writer = NULL;
    cx_string *text = NULL;
    CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, &g_alloc, NULL));
    CA_CHECK_EQ(CA_OK, ca_track_write_json(&track, writer, NULL, NULL));
    CA_CHECK_EQ(CA_OK, ca_json_writer_finish(writer, &text, NULL));
    ca_json_writer_destroy(&writer);
    CA_CHECK(text != NULL);
    const char *json = text ? cx_string_cstr(text) : "";
    CA_CHECK(strstr(json, "\"id\":7,\"size\":1234,\"mtime\":1700000000,") !=
             NULL);
    CA_CHECK(strstr(json, "\"duration\":3.5,") != NULL);
    CA_CHECK(strstr(json, "\"fingerprint\":\"000102030405060708090a0b0c0d0e0f"
                         "101112131415161718191a1b1c1d1e1f\",") != NULL);
    CA_CHECK(strstr(json, "\"path\":\"/music/a.mp3\",\"source\":{\"kind\":"
                         "\"external\",\"key\":\"content://x\","
                         "\"display_name\":\"Song\",\"available\":false},") !=
             NULL);
    CA_CHECK(strstr(json, "\"cover_art_path\":\"/art.jpg\"}") != NULL);
    cx_string_destroy(&text);
    ca_track_destroy(&track);

    /* Integral doubles keep their floating-point form. */
    ca_track whole;
    CA_CHECK_EQ(CA_OK, ca_track_init(&whole, &g_alloc));
    whole.duration = 2.0;
    CA_CHECK_EQ(CX_OK, cx_string_assign(whole.path, "/x"));
    writer = NULL;
    text = NULL;
    CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, &g_alloc, NULL));
    CA_CHECK_EQ(CA_OK, ca_track_write_json(&whole, writer, NULL, NULL));
    CA_CHECK_EQ(CA_OK, ca_json_writer_finish(writer, &text, NULL));
    ca_json_writer_destroy(&writer);
    CA_CHECK(text != NULL && strstr(cx_string_cstr(text), "\"duration\":2.0,") !=
                                NULL);
    cx_string_destroy(&text);
    ca_track_destroy(&whole);
}

static void test_track_from_json_defaults(void) {
    ca_json *doc = parse_text(
        "{\"path\":\"/music/a.mp3\",\"library_id\":0,\"dirty\":1}");
    CA_CHECK(doc != NULL);
    if (doc) {
        ca_track track;
        CA_CHECK_EQ(CA_OK,
                    ca_track_from_json(&track, ca_json_root(doc), &g_alloc,
                                       NULL));
        CA_CHECK_EQ(1, track.library_id);
        CA_CHECK(track.dirty);
        CA_CHECK(track.source.kind == CA_SOURCE_PATH);
        CA_CHECK(track.source.key != NULL);
        CA_CHECK(cx_string_view_eq(cx_string_view_of(track.source.key),
                                   cx_string_view_from_cstr("/music/a.mp3")));
        /* Missing fingerprint falls back to the path digest. */
        static const char expected[] =
            "4b5e26fae62ba091838993e24b3efb10bb867b1f4da58f4af3b7debbd50dc3b9";
        static const char digits[] = "0123456789abcdef";
        char hex[65];
        for (size_t i = 0; i < 32; ++i) {
            hex[i * 2] = digits[track.fingerprint[i] >> 4];
            hex[i * 2 + 1] = digits[track.fingerprint[i] & 0x0f];
        }
        hex[64] = '\0';
        CA_CHECK(strcmp(hex, expected) == 0);
        ca_track_destroy(&track);
        ca_json_destroy(&doc);
    }

    /* A malformed hex fingerprint also falls back. */
    doc = parse_text(
        "{\"path\":\"/music/a.mp3\",\"fingerprint\":\"zzzz\"}");
    CA_CHECK(doc != NULL);
    if (doc) {
        ca_track track;
        CA_CHECK_EQ(CA_OK,
                    ca_track_from_json(&track, ca_json_root(doc), &g_alloc,
                                       NULL));
        CA_CHECK(track.fingerprint[0] == 0x4b);
        ca_track_destroy(&track);
        ca_json_destroy(&doc);
    }

    /* An unknown source kind is rejected and leaves the record zeroed. */
    doc = parse_text(
        "{\"path\":\"/x\",\"source\":{\"kind\":\"bogus\"}}");
    CA_CHECK(doc != NULL);
    if (doc) {
        ca_track track;
        CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                    ca_track_from_json(&track, ca_json_root(doc), &g_alloc,
                                       NULL));
        CA_CHECK(track.path == NULL);
        ca_json_destroy(&doc);
    }
}

static void test_simple_records(void) {
    ca_playlist playlist;
    CA_CHECK_EQ(CA_OK, ca_playlist_init(&playlist, &g_alloc));
    playlist.id = 3;
    playlist.type = 1;
    playlist.created = 100;
    playlist.modified = 200;
    CA_CHECK_EQ(CX_OK, cx_string_assign(playlist.name, "Favorites"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(playlist.smart_query, "rating > 3"));
    ROUNDTRIP(ca_playlist,
              ca_playlist_write_json(&playlist, w1, NULL, NULL),
              ca_playlist_from_json(&decoded, root, &g_alloc, NULL),
              ca_playlist_write_json(&decoded, w2, NULL, NULL),
              ca_playlist_destroy(&decoded));
    ca_playlist_destroy(&playlist);

    ca_queue_item item;
    CA_CHECK_EQ(CA_OK, ca_queue_item_init(&item, &g_alloc));
    item.id = 5;
    item.track_id = 9;
    item.position = 2;
    item.added = 300;
    ROUNDTRIP(ca_queue_item,
              ca_queue_item_write_json(&item, w1, NULL, NULL),
              ca_queue_item_from_json(&decoded, root, &g_alloc, NULL),
              ca_queue_item_write_json(&decoded, w2, NULL, NULL),
              ca_queue_item_destroy(&decoded));

    ca_history_entry entry;
    CA_CHECK_EQ(CA_OK, ca_history_entry_init(&entry, &g_alloc));
    entry.id = 1;
    entry.track_id = 2;
    entry.started_at = 10;
    entry.completed_at = 20;
    entry.position_ms = 3000;
    entry.completion_pct = 87.5;
    entry.queue_id = 4;
    entry.duration = 240.25;
    CA_CHECK_EQ(CX_OK, cx_string_assign(entry.title, "T"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(entry.artist, "A"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(entry.path, "/p"));
    ROUNDTRIP(ca_history_entry,
              ca_history_entry_write_json(&entry, w1, NULL, NULL),
              ca_history_entry_from_json(&decoded, root, &g_alloc, NULL),
              ca_history_entry_write_json(&decoded, w2, NULL, NULL),
              ca_history_entry_destroy(&decoded));
    ca_history_entry_destroy(&entry);

    ca_bookmark bookmark;
    CA_CHECK_EQ(CA_OK, ca_bookmark_init(&bookmark, &g_alloc));
    bookmark.id = 6;
    bookmark.track_id = 7;
    bookmark.position_ms = 12345;
    bookmark.created = 999;
    CA_CHECK_EQ(CX_OK, cx_string_assign(bookmark.note, "resume"));
    ROUNDTRIP(ca_bookmark,
              ca_bookmark_write_json(&bookmark, w1, NULL, NULL),
              ca_bookmark_from_json(&decoded, root, &g_alloc, NULL),
              ca_bookmark_write_json(&decoded, w2, NULL, NULL),
              ca_bookmark_destroy(&decoded));
    ca_bookmark_destroy(&bookmark);

    ca_track_metadata metadata;
    CA_CHECK_EQ(CA_OK, ca_track_metadata_init(&metadata, &g_alloc));
    metadata.year = 1999;
    metadata.track_num = 4;
    metadata.disc_num = 2;
    metadata.duration = 111.5;
    metadata.sample_rate = 44100;
    metadata.channels = 2;
    metadata.bitrate = 192000;
    CA_CHECK_EQ(CX_OK, cx_string_assign(metadata.title, "M"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(metadata.artist, "N"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(metadata.album, "O"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(metadata.album_artist, "P"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(metadata.genre, "Q"));
    ROUNDTRIP(ca_track_metadata,
              ca_track_metadata_write_json(&metadata, w1, NULL, NULL),
              ca_track_metadata_from_json(&decoded, root, &g_alloc, NULL),
              ca_track_metadata_write_json(&decoded, w2, NULL, NULL),
              ca_track_metadata_destroy(&decoded));
    ca_track_metadata_destroy(&metadata);

    ca_top_entity entity;
    CA_CHECK_EQ(CA_OK, ca_top_entity_init(&entity, &g_alloc));
    entity.track_count = 12;
    entity.total_plays = 340;
    CA_CHECK_EQ(CX_OK, cx_string_assign(entity.name, "Artist"));
    ROUNDTRIP(ca_top_entity,
              ca_top_entity_write_json(&entity, w1, NULL, NULL),
              ca_top_entity_from_json(&decoded, root, &g_alloc, NULL),
              ca_top_entity_write_json(&decoded, w2, NULL, NULL),
              ca_top_entity_destroy(&decoded));
    ca_top_entity_destroy(&entity);

    ca_queue queue;
    CA_CHECK_EQ(CA_OK, ca_queue_init(&queue, &g_alloc));
    queue.id = 1;
    queue.repeat_mode = CA_REPEAT_ALL;
    queue.temp = true;
    CA_CHECK_EQ(CX_OK, cx_string_assign(queue.name, "default"));
    ROUNDTRIP(ca_queue, ca_queue_write_json(&queue, w1, NULL, NULL),
              ca_queue_from_json(&decoded, root, &g_alloc, NULL),
              ca_queue_write_json(&decoded, w2, NULL, NULL),
              ca_queue_destroy(&decoded));
    ca_queue_destroy(&queue);

    ca_library library;
    CA_CHECK_EQ(CA_OK, ca_library_init(&library, &g_alloc));
    library.id = 1;
    library.date_added = 5;
    library.last_scanned = 6;
    CA_CHECK_EQ(CX_OK, cx_string_assign(library.path, "/music"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(library.name, "Main"));
    ROUNDTRIP(ca_library, ca_library_write_json(&library, w1, NULL, NULL),
              ca_library_from_json(&decoded, root, &g_alloc, NULL),
              ca_library_write_json(&decoded, w2, NULL, NULL),
              ca_library_destroy(&decoded));
    ca_library_destroy(&library);

    ca_playback_stats stats;
    CA_CHECK_EQ(CA_OK, ca_playback_stats_init(&stats, &g_alloc));
    stats.num_tracks = 10;
    stats.num_playlists = 2;
    stats.num_queue_items = 8;
    stats.num_history = 30;
    stats.num_bookmarks = 1;
    stats.num_libraries = 1;
    stats.total_duration_ms = 123456;
    ROUNDTRIP(ca_playback_stats,
              ca_playback_stats_write_json(&stats, w1, NULL, NULL),
              ca_playback_stats_from_json(&decoded, root, &g_alloc, NULL),
              ca_playback_stats_write_json(&decoded, w2, NULL, NULL),
              ca_playback_stats_destroy(&decoded));
}

static void test_devices(void) {
    ca_device_info device;
    CA_CHECK_EQ(CA_OK, ca_device_info_init(&device, &g_alloc));
    device.is_default = true;
    CA_CHECK_EQ(CX_OK, cx_string_assign(device.id, "dev0"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(device.name, "Speakers"));

    ROUNDTRIP(ca_device_info,
              ca_device_info_write_json(&device, w1, NULL, NULL),
              ca_device_info_from_json(&decoded, root, &g_alloc, NULL),
              ca_device_info_write_json(&decoded, w2, NULL, NULL),
              ca_device_info_destroy(&decoded));
    ca_device_info_destroy(&device);

    ca_json *doc = parse_text(
        "{\"devices\":[{\"id\":\"a\",\"name\":\"A\",\"isDefault\":true},"
        "{\"id\":\"b\",\"name\":\"B\",\"isDefault\":false}]}");
    CA_CHECK(doc != NULL);
    if (doc) {
        ca_device_list list;
        CA_CHECK_EQ(CA_OK, ca_device_list_from_json(&list, ca_json_root(doc),
                                                    &g_alloc, NULL));
        CA_CHECK_EQ(2, cx_vector_length(list.devices));
        const ca_device_info *first =
            (const ca_device_info *)cx_vector_ptr(list.devices, 0);
        CA_CHECK(first != NULL && first->is_default);

        ca_json_writer *writer = NULL;
        cx_string *text = NULL;
        CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, &g_alloc, NULL));
        CA_CHECK_EQ(CA_OK, ca_device_list_write_json(&list, writer, NULL, NULL));
        CA_CHECK_EQ(CA_OK, ca_json_writer_finish(writer, &text, NULL));
        ca_json_writer_destroy(&writer);
        CA_CHECK(text != NULL &&
                 strstr(cx_string_cstr(text), "\"isDefault\":true") != NULL);
        cx_string_destroy(&text);
        ca_device_list_destroy(&list);
        ca_json_destroy(&doc);
    }
}

static void test_library_stats(void) {
    ca_json *doc = parse_text(
        "{\"tracks\":3,\"queues\":2,\"playlists\":1,\"total_duration_ms\":1000,"
        "\"total_play_time_ms\":2000,"
        "\"top_tracks\":[{\"id\":1,\"path\":\"/a.mp3\",\"duration\":2.0}],"
        "\"top_albums\":[{\"name\":\"A\",\"track_count\":2,"
        "\"total_plays\":5}],"
        "\"top_genres\":[{\"name\":\"Rock\",\"track_count\":1,"
        "\"total_plays\":9}],"
        "\"top_artists\":[{\"name\":\"X\",\"track_count\":4,"
        "\"total_plays\":12}]}");
    CA_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    ca_library_stats stats;
    CA_CHECK_EQ(CA_OK,
                ca_library_stats_from_json(&stats, ca_json_root(doc), &g_alloc,
                                           NULL));
    CA_CHECK_EQ(3, stats.tracks);
    CA_CHECK_EQ(2, stats.queues);
    CA_CHECK(stats.has_top_tracks);
    CA_CHECK_EQ(1, cx_vector_length(stats.top_tracks));
    const ca_track *top =
        (const ca_track *)cx_vector_ptr(stats.top_tracks, 0);
    CA_CHECK(top != NULL && top->id == 1);
    CA_CHECK(stats.has_top_artists);
    const ca_top_entity *artist =
        (const ca_top_entity *)cx_vector_ptr(stats.top_artists, 0);
    CA_CHECK(artist != NULL && artist->total_plays == 12);

    ca_json_writer *writer = NULL;
    cx_string *first = NULL;
    cx_string *second = NULL;
    CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, &g_alloc, NULL));
    CA_CHECK_EQ(CA_OK, ca_library_stats_write_json(&stats, writer, NULL, NULL));
    CA_CHECK_EQ(CA_OK, ca_json_writer_finish(writer, &first, NULL));
    ca_json_writer_destroy(&writer);

    ca_json *again = first ? parse_string(first) : NULL;
    CA_CHECK(again != NULL);
    if (again) {
        ca_library_stats copy;
        CA_CHECK_EQ(CA_OK, ca_library_stats_from_json(
                               &copy, ca_json_root(again), &g_alloc, NULL));
        CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, &g_alloc, NULL));
        CA_CHECK_EQ(CA_OK,
                    ca_library_stats_write_json(&copy, writer, NULL, NULL));
        CA_CHECK_EQ(CA_OK, ca_json_writer_finish(writer, &second, NULL));
        ca_json_writer_destroy(&writer);
        CA_CHECK(view_eq(first, second));
        ca_library_stats_destroy(&copy);
        ca_json_destroy(&again);
    }

    CA_CHECK(first != NULL &&
             strstr(cx_string_cstr(first), "\"top_artists\":[{\"name\":\"X\","
                                           "\"track_count\":4,\"total_plays\":"
                                           "12}]") != NULL);
    cx_string_destroy(&first);
    cx_string_destroy(&second);
    ca_library_stats_destroy(&stats);
    ca_json_destroy(&doc);

    /* Present-but-empty top lists keep the "requested" flag. */
    doc = parse_text("{\"top_tracks\":[]}");
    CA_CHECK(doc != NULL);
    if (doc) {
        ca_library_stats empty;
        CA_CHECK_EQ(CA_OK, ca_library_stats_from_json(
                               &empty, ca_json_root(doc), &g_alloc, NULL));
        CA_CHECK(empty.has_top_tracks);
        CA_CHECK(empty.top_tracks != NULL);
        CA_CHECK_EQ(0, cx_vector_length(empty.top_tracks));
        ca_library_stats_destroy(&empty);
        ca_json_destroy(&doc);
    }
}

static void test_queries(void) {
    ca_track_query query;
    CA_CHECK_EQ(CA_OK, ca_track_query_init(&query, &g_alloc));
    CA_CHECK_EQ(CX_OK, cx_string_assign(query.artist, "A"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(query.search, "needle"));
    query.has_year = true;
    query.year = 2001;
    query.has_library_id = true;
    query.library_id = 4;
    query.has_dirty = true;
    query.dirty = true;
    query.has_duration_min = true;
    query.duration_min = 1.25;
    query.has_duration_max = true;
    query.duration_max = 300.5;
    query.limit = 20;
    query.offset = 5;

    ROUNDTRIP(ca_track_query,
              ca_track_query_write_json(&query, w1, NULL, NULL),
              ca_track_query_from_json(&decoded, root, &g_alloc, NULL),
              ca_track_query_write_json(&decoded, w2, NULL, NULL),
              ca_track_query_destroy(&decoded));

    /* Unset optionals serialize as null. */
    ca_track_query empty;
    CA_CHECK_EQ(CA_OK, ca_track_query_init(&empty, &g_alloc));
    ca_json_writer *writer = NULL;
    cx_string *text = NULL;
    CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, &g_alloc, NULL));
    CA_CHECK_EQ(CA_OK, ca_track_query_write_json(&empty, writer, NULL, NULL));
    CA_CHECK_EQ(CA_OK, ca_json_writer_finish(writer, &text, NULL));
    ca_json_writer_destroy(&writer);
    CA_CHECK(text != NULL &&
             strstr(cx_string_cstr(text), "\"year\":null") != NULL);
    cx_string_destroy(&text);
    ca_track_query_destroy(&empty);
    ca_track_query_destroy(&query);

    ca_history_query history;
    CA_CHECK_EQ(CA_OK, ca_history_query_init(&history, &g_alloc));
    history.has_track_id = true;
    history.track_id = 12;
    history.limit = 10;
    ROUNDTRIP(ca_history_query,
              ca_history_query_write_json(&history, w1, NULL, NULL),
              ca_history_query_from_json(&decoded, root, &g_alloc, NULL),
              ca_history_query_write_json(&decoded, w2, NULL, NULL),
              ca_history_query_destroy(&decoded));
}

static void test_engine_records(void) {
    ca_engine_event event;
    CA_CHECK_EQ(CA_OK, ca_engine_event_init(&event, &g_alloc));
    event.type = CA_ENGINE_EVENT_PROGRESS;
    event.track_id = 3;
    event.queue_id = 2;
    event.position = 1.5;
    event.duration = 10.0;
    CA_CHECK_EQ(CX_OK, cx_string_assign(event.msg, "tick"));
    ROUNDTRIP(ca_engine_event,
              ca_engine_event_write_json(&event, w1, NULL, NULL),
              ca_engine_event_from_json(&decoded, root, &g_alloc, NULL),
              ca_engine_event_write_json(&decoded, w2, NULL, NULL),
              ca_engine_event_destroy(&decoded));
    ca_engine_event_destroy(&event);

    ca_engine_state state;
    CA_CHECK_EQ(CA_OK, ca_engine_state_init(&state, &g_alloc));
    state.shuffle_enabled = 1;
    state.repeat_mode = CA_REPEAT_ONE;
    state.cursor_pos = 2;
    state.current_track_id = 9;
    state.volume = 0.75f;
    state.active_queue_id = 3;
    state.pre_mute_volume = 0.25f;
    state.position_ms = 1234;
    for (int64_t i = 0; i < 3; ++i) {
        CA_CHECK_EQ(CX_OK,
                    cx_vector_push_copy(state.shuffle_perm, &i, sizeof i));
    }
    ROUNDTRIP(ca_engine_state,
              ca_engine_state_write_json(&state, w1, NULL, NULL),
              ca_engine_state_from_json(&decoded, root, &g_alloc, NULL),
              ca_engine_state_write_json(&decoded, w2, NULL, NULL),
              ca_engine_state_destroy(&decoded));
    ca_engine_state_destroy(&state);

    /* Parsing a shuffle permutation preserves order and count. */
    ca_json *doc = parse_text(
        "{\"shuffle_enabled\":1,\"repeat_mode\":2,\"shuffle_perm\":[4,5,6]}");
    CA_CHECK(doc != NULL);
    if (doc) {
        ca_engine_state parsed;
        CA_CHECK_EQ(CA_OK, ca_engine_state_from_json(
                               &parsed, ca_json_root(doc), &g_alloc, NULL));
        CA_CHECK(parsed.shuffle_enabled);
        CA_CHECK(parsed.repeat_mode == CA_REPEAT_ONE);
        CA_CHECK_EQ(3, cx_vector_length(parsed.shuffle_perm));
        CA_CHECK(*(const int64_t *)cx_vector_ptr(parsed.shuffle_perm, 1) == 5);
        ca_engine_state_destroy(&parsed);
        ca_json_destroy(&doc);
    }
}

static void test_writer_errors(void) {
    ca_json_writer *writer = NULL;
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_json_writer_init(NULL, 0, NULL, NULL));

    cx_string *text = NULL;
    CA_CHECK_EQ(CA_OK, ca_json_writer_init(&writer, 0, &g_alloc, NULL));
    /* Nothing written yet. */
    CA_CHECK_EQ(CA_JSON_ERROR, ca_json_writer_finish(writer, &text, NULL));
    /* Object member without a key. */
    CA_CHECK_EQ(CA_OK, ca_json_writer_begin_object(writer, NULL, NULL));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_json_writer_add_int(writer, NULL, 1, NULL));
    CA_CHECK_EQ(CA_OK, ca_json_writer_end_object(writer, NULL));
    /* The root object already completed, so a second root value is rejected. */
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_json_writer_add_int(writer, NULL, 1, NULL));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT,
                ca_json_writer_add_int(writer, NULL, 2, NULL));
    CA_CHECK_EQ(CA_OK, ca_json_writer_finish(writer, &text, NULL));
    CA_CHECK(strcmp(cx_string_cstr(text), "{}") == 0);
    cx_string_destroy(&text);
    ca_json_writer_destroy(&writer);

    /* NULL-safe destruction. */
    ca_json_writer_destroy(NULL);
    ca_json_writer_destroy(&writer);
}

int main(void) {
    CA_CHECK_EQ(CX_OK, cx_allocator_counted_init(&g_counted, NULL));
    g_alloc = cx_allocator_counted_as_allocator(&g_counted);

    test_track_json();
    test_track_from_json_defaults();
    test_simple_records();
    test_devices();
    test_library_stats();
    test_queries();
    test_engine_records();
    test_writer_errors();

    CA_CHECK(!cx_allocator_counted_has_leaks(&g_counted));
    CA_CHECK_EQ(0, g_counted.live_blocks);
    cx_allocator_counted_destroy(&g_counted);
    return ca_test_finish("types_json");
}