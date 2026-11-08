#include <caudio/types/record.h>
#include <caudio/types/wire.h>

#include <cstdx/allocator_counted.h>
#include <cstdx/string.h>
#include <cstdx/string_view.h>

#include <string.h>

#include "ca_test.h"

static bool str_eq(const cx_string *value, const char *expected) {
    if (!value) {
        return expected == nullptr || expected[0] == '\0';
    }
    return cx_string_view_eq(cx_string_view_of(value),
                             cx_string_view_from_cstr(expected));
}

static void test_wire_names(void) {
    CA_CHECK(strcmp(ca_repeat_mode_name(CA_REPEAT_OFF), "Off") == 0);
    CA_CHECK(strcmp(ca_repeat_mode_name(CA_REPEAT_ALL), "All") == 0);
    CA_CHECK(strcmp(ca_repeat_mode_name(CA_REPEAT_ONE), "One") == 0);
    CA_CHECK(strcmp(ca_repeat_mode_name((ca_repeat_mode)99), "Unknown") == 0);

    ca_repeat_mode mode = CA_REPEAT_OFF;
    CA_CHECK(ca_repeat_mode_parse("Off", &mode) && mode == CA_REPEAT_OFF);
    CA_CHECK(ca_repeat_mode_parse("All", &mode) && mode == CA_REPEAT_ALL);
    CA_CHECK(ca_repeat_mode_parse("One", &mode) && mode == CA_REPEAT_ONE);
    CA_CHECK(!ca_repeat_mode_parse("off", &mode));
    CA_CHECK(!ca_repeat_mode_parse("Unknown", &mode));
    CA_CHECK(!ca_repeat_mode_parse(nullptr, &mode));
    CA_CHECK(!ca_repeat_mode_parse("Off", nullptr));

    CA_CHECK(strcmp(ca_playback_state_name(CA_PLAYBACK_STOPPED), "Stopped") == 0);
    CA_CHECK(strcmp(ca_playback_state_name(CA_PLAYBACK_READY), "Ready") == 0);
    CA_CHECK(strcmp(ca_playback_state_name(CA_PLAYBACK_PLAYING), "Playing") == 0);
    CA_CHECK(strcmp(ca_playback_state_name(CA_PLAYBACK_PAUSED), "Paused") == 0);
    CA_CHECK(strcmp(ca_playback_state_name((ca_playback_state)99), "Unknown") ==
             0);

    ca_playback_state state = CA_PLAYBACK_STOPPED;
    CA_CHECK(ca_playback_state_parse("Stopped", &state) &&
             state == CA_PLAYBACK_STOPPED);
    CA_CHECK(ca_playback_state_parse("Ready", &state) &&
             state == CA_PLAYBACK_READY);
    CA_CHECK(ca_playback_state_parse("Playing", &state) &&
             state == CA_PLAYBACK_PLAYING);
    CA_CHECK(ca_playback_state_parse("Paused", &state) &&
             state == CA_PLAYBACK_PAUSED);
    CA_CHECK(!ca_playback_state_parse("playing", &state));

    CA_CHECK(strcmp(ca_source_kind_name(CA_SOURCE_PATH), "path") == 0);
    CA_CHECK(strcmp(ca_source_kind_name(CA_SOURCE_EXTERNAL), "external") == 0);
    CA_CHECK(strcmp(ca_source_kind_name(CA_SOURCE_IMPORTED), "imported") == 0);
    CA_CHECK(strcmp(ca_source_kind_name(CA_SOURCE_UNAVAILABLE), "unavailable") ==
             0);
    /* Unknown source kinds spell as the legacy default. */
    CA_CHECK(strcmp(ca_source_kind_name((ca_source_kind)99), "path") == 0);

    ca_source_kind kind = CA_SOURCE_PATH;
    CA_CHECK(ca_source_kind_parse("external", &kind) &&
             kind == CA_SOURCE_EXTERNAL);
    CA_CHECK(ca_source_kind_parse("imported", &kind) &&
             kind == CA_SOURCE_IMPORTED);
    CA_CHECK(ca_source_kind_parse("unavailable", &kind) &&
             kind == CA_SOURCE_UNAVAILABLE);
    CA_CHECK(!ca_source_kind_parse("Path", &kind));
    CA_CHECK(!ca_source_kind_parse("nope", &kind));

    CA_CHECK(strcmp(ca_engine_event_type_name(CA_ENGINE_EVENT_NONE), "None") ==
             0);
    CA_CHECK(strcmp(ca_engine_event_type_name(CA_ENGINE_EVENT_TRACK_STARTED),
                    "TrackStarted") == 0);
    CA_CHECK(strcmp(ca_engine_event_type_name(CA_ENGINE_EVENT_TRACK_ENDED),
                    "TrackEnded") == 0);
    CA_CHECK(strcmp(ca_engine_event_type_name(CA_ENGINE_EVENT_QUEUE_CHANGED),
                    "QueueChanged") == 0);
    CA_CHECK(strcmp(ca_engine_event_type_name(CA_ENGINE_EVENT_PROGRESS),
                    "Progress") == 0);
    CA_CHECK(strcmp(ca_engine_event_type_name(CA_ENGINE_EVENT_ERROR), "Error") ==
             0);
    CA_CHECK(strcmp(ca_engine_event_type_name((ca_engine_event_type)99),
                    "Unknown") == 0);

    ca_engine_event_type type = CA_ENGINE_EVENT_NONE;
    CA_CHECK(ca_engine_event_type_parse("QueueChanged", &type) &&
             type == CA_ENGINE_EVENT_QUEUE_CHANGED);
    CA_CHECK(ca_engine_event_type_parse("Progress", &type) &&
             type == CA_ENGINE_EVENT_PROGRESS);
    CA_CHECK(!ca_engine_event_type_parse("progress", &type));
}

static void test_track_lifecycle(void) {
    ca_track track;
    CA_CHECK_EQ(CA_OK, ca_track_init(&track, nullptr));
    CA_CHECK_EQ(1, track.library_id);
    CA_CHECK(track.path != nullptr && track.title != nullptr && track.artist != nullptr);
    CA_CHECK(str_eq(track.path, ""));
    CA_CHECK(track.source.kind == CA_SOURCE_PATH);
    CA_CHECK(track.source.available);
    CA_CHECK(track.source.key != nullptr && track.source.display_name != nullptr);

    CA_CHECK_EQ(CX_OK, cx_string_assign(track.path, "/music/one.mp3"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.title, "One"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.artist, "Artist"));
    track.id = 42;
    track.duration = 12.5;
    track.sample_rate = 48000;
    track.dirty = true;
    memset(track.fingerprint, 0xab, sizeof track.fingerprint);

    ca_track copy;
    CA_CHECK_EQ(CA_OK, ca_track_copy(&copy, &track, nullptr));
    CA_CHECK_EQ(42, copy.id);
    CA_CHECK(copy.duration == 12.5);
    CA_CHECK_EQ(48000, copy.sample_rate);
    CA_CHECK(copy.dirty);
    CA_CHECK(str_eq(copy.path, "/music/one.mp3"));
    CA_CHECK(str_eq(copy.title, "One"));
    CA_CHECK(copy.path != track.path);
    CA_CHECK(memcmp(copy.fingerprint, track.fingerprint, 32) == 0);

    /* Deep copies: mutating the original must not affect the copy. */
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.title, "Changed"));
    CA_CHECK(str_eq(copy.title, "One"));

    ca_track moved;
    memset(&moved, 0, sizeof moved);
    ca_track_move(&moved, &copy);
    CA_CHECK(str_eq(moved.title, "One"));
    CA_CHECK(copy.path == nullptr && copy.title == nullptr && copy.source.key == nullptr);

    ca_track_destroy(&moved);
    CA_CHECK(moved.path == nullptr);

    /* Destroying a move source is a no-op. */
    ca_track_destroy(&copy);
    ca_track_destroy(&track);
}

static void test_normalized_source(void) {
    ca_track track;
    CA_CHECK_EQ(CA_OK, ca_track_init(&track, nullptr));
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.path, "/music/two.flac"));

    ca_source_ref source;
    CA_CHECK_EQ(CA_OK, ca_track_normalized_source(&track, &source, nullptr));
    CA_CHECK(source.kind == CA_SOURCE_PATH);
    CA_CHECK(str_eq(source.key, "/music/two.flac"));
    CA_CHECK(str_eq(source.display_name, "/music/two.flac"));
    CA_CHECK(source.available);
    ca_source_ref_destroy(&source);

    /* An explicit key is preserved; an empty display name falls back. */
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.source.key, "stable-key"));
    CA_CHECK_EQ(CA_OK, ca_track_normalized_source(&track, &source, nullptr));
    CA_CHECK(str_eq(source.key, "stable-key"));
    CA_CHECK(str_eq(source.display_name, "/music/two.flac"));
    ca_source_ref_destroy(&source);

    /* Non-path sources are returned unchanged. */
    track.source.kind = CA_SOURCE_EXTERNAL;
    cx_string_clear(track.source.key);
    cx_string_clear(track.source.display_name);
    CA_CHECK_EQ(CA_OK, ca_track_normalized_source(&track, &source, nullptr));
    CA_CHECK(source.kind == CA_SOURCE_EXTERNAL);
    CA_CHECK(str_eq(source.key, ""));
    CA_CHECK(str_eq(source.display_name, ""));
    ca_source_ref_destroy(&source);

    ca_track_destroy(&track);
}

static void test_components(void) {
    ca_playlist playlist;
    CA_CHECK_EQ(CA_OK, ca_playlist_init(&playlist, nullptr));
    CA_CHECK_EQ(1, playlist.library_id);
    CA_CHECK_EQ(CX_OK, cx_string_assign(playlist.name, "Favorites"));
    ca_playlist playlist_copy;
    CA_CHECK_EQ(CA_OK, ca_playlist_copy(&playlist_copy, &playlist, nullptr));
    CA_CHECK(str_eq(playlist_copy.name, "Favorites"));
    ca_playlist_destroy(&playlist_copy);
    ca_playlist_destroy(&playlist);

    ca_library library;
    CA_CHECK_EQ(CA_OK, ca_library_init(&library, nullptr));
    CA_CHECK(library.auto_scan);
    CA_CHECK(library.recursive);
    CA_CHECK(str_eq(library.extensions, "mp3,flac,ogg,wav,m4a"));
    ca_library_destroy(&library);

    ca_device_list devices;
    CA_CHECK_EQ(CA_OK, ca_device_list_init(&devices, nullptr));
    CA_CHECK(devices.devices != nullptr);
    CA_CHECK_EQ(0, cx_vector_length(devices.devices));

    ca_device_info device;
    CA_CHECK_EQ(CA_OK, ca_device_info_init(&device, nullptr));
    CA_CHECK_EQ(CX_OK, cx_string_assign(device.id, "default"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(device.name, "Speakers"));
    device.is_default = true;
    CA_CHECK_EQ(CX_OK, cx_vector_push_copy(devices.devices, &device,
                                           sizeof device));
    ca_device_info_destroy(&device);

    ca_device_list devices_copy;
    CA_CHECK_EQ(CA_OK, ca_device_list_copy(&devices_copy, &devices, nullptr));
    CA_CHECK_EQ(1, cx_vector_length(devices_copy.devices));
    const ca_device_info *copied =
        (const ca_device_info *)cx_vector_ptr(devices_copy.devices, 0);
    CA_CHECK(copied != nullptr);
    CA_CHECK(str_eq(copied->id, "default"));
    CA_CHECK(copied->is_default);

    ca_device_list moved;
    memset(&moved, 0, sizeof moved);
    ca_device_list_move(&moved, &devices_copy);
    CA_CHECK(devices_copy.devices == nullptr);
    ca_device_list_destroy(&moved);
    ca_device_list_destroy(&devices);

    ca_engine_state state;
    CA_CHECK_EQ(CA_OK, ca_engine_state_init(&state, nullptr));
    CA_CHECK(state.shuffle_perm != nullptr);
    CA_CHECK(state.repeat_mode == CA_REPEAT_OFF);
    CA_CHECK(state.volume == 1.0f);
    CA_CHECK_EQ(1, state.active_queue_id);
    for (int64_t i = 0; i < 4; ++i) {
        CA_CHECK_EQ(CX_OK,
                    cx_vector_push_copy(state.shuffle_perm, &i, sizeof i));
    }
    ca_engine_state state_copy;
    CA_CHECK_EQ(CA_OK, ca_engine_state_copy(&state_copy, &state, nullptr));
    CA_CHECK_EQ(4, cx_vector_length(state_copy.shuffle_perm));
    CA_CHECK(*(const int64_t *)cx_vector_ptr(state_copy.shuffle_perm, 3) == 3);
    ca_engine_state_destroy(&state_copy);
    ca_engine_state_destroy(&state);

    ca_library_stats stats;
    CA_CHECK_EQ(CA_OK, ca_library_stats_init(&stats, nullptr));
    stats.has_top_tracks = true;
    stats.top_tracks = nullptr;
    ca_library_stats copy_stats;
    CA_CHECK_EQ(CA_OK, ca_library_stats_copy(&copy_stats, &stats, nullptr));
    CA_CHECK(copy_stats.has_top_tracks);
    CA_CHECK(copy_stats.top_tracks != nullptr);
    CA_CHECK_EQ(0, cx_vector_length(copy_stats.top_tracks));
    CA_CHECK(!copy_stats.has_top_albums);
    ca_library_stats_destroy(&copy_stats);
    ca_library_stats_destroy(&stats);

    ca_track_query query;
    CA_CHECK_EQ(CA_OK, ca_track_query_init(&query, nullptr));
    query.has_year = true;
    query.year = 1999;
    CA_CHECK_EQ(CX_OK, cx_string_assign(query.sort, "year"));
    ca_track_query copy_query;
    CA_CHECK_EQ(CA_OK, ca_track_query_copy(&copy_query, &query, nullptr));
    CA_CHECK(copy_query.has_year);
    CA_CHECK_EQ(1999, copy_query.year);
    CA_CHECK(str_eq(copy_query.sort, "year"));
    ca_track_query_destroy(&copy_query);
    ca_track_query_destroy(&query);

    ca_engine_event event;
    CA_CHECK_EQ(CA_OK, ca_engine_event_init(&event, nullptr));
    CA_CHECK(event.type == CA_ENGINE_EVENT_NONE);
    CA_CHECK_EQ(1, event.queue_id);
    CA_CHECK_EQ(CX_OK, cx_string_assign(event.msg, "boom"));
    ca_engine_event copy_event;
    CA_CHECK_EQ(CA_OK, ca_engine_event_copy(&copy_event, &event, nullptr));
    CA_CHECK(str_eq(copy_event.msg, "boom"));
    ca_engine_event_destroy(&copy_event);
    ca_engine_event_destroy(&event);
}

static void test_invalid_arguments(void) {
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_track_init(nullptr, nullptr));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_playlist_init(nullptr, nullptr));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_queue_init(nullptr, nullptr));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_library_init(nullptr, nullptr));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_track_copy(nullptr, nullptr, nullptr));
    CA_CHECK_EQ(CA_INVALID_ARGUMENT, ca_source_ref_copy(nullptr, nullptr, nullptr));

    ca_track track;
    memset(&track, 0, sizeof track);
    ca_track_destroy(&track);
    ca_track_move(nullptr, nullptr);
    ca_source_ref_destroy(nullptr);
}

static void test_leak_free(void) {
    cx_allocator_counted counted;
    CA_CHECK_EQ(CX_OK, cx_allocator_counted_init(&counted, nullptr));
    cx_allocator alloc = cx_allocator_counted_as_allocator(&counted);

    ca_track track;
    CA_CHECK_EQ(CA_OK, ca_track_init(&track, &alloc));
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.path, "/a/b.mp3"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.title, "Title"));
    CA_CHECK_EQ(CX_OK, cx_string_assign(track.source.key, "key"));

    ca_track copy;
    CA_CHECK_EQ(CA_OK, ca_track_copy(&copy, &track, &alloc));
    ca_track_destroy(&copy);
    ca_track_destroy(&track);

    CA_CHECK(!cx_allocator_counted_has_leaks(&counted));
    CA_CHECK_EQ(0, counted.live_blocks);

    ca_engine_state state;
    CA_CHECK_EQ(CA_OK, ca_engine_state_init(&state, &alloc));
    for (int64_t i = 0; i < 3; ++i) {
        CA_CHECK_EQ(CX_OK,
                    cx_vector_push_copy(state.shuffle_perm, &i, sizeof i));
    }
    ca_engine_state state_copy;
    CA_CHECK_EQ(CA_OK, ca_engine_state_copy(&state_copy, &state, &alloc));
    ca_engine_state_destroy(&state_copy);
    ca_engine_state_destroy(&state);

    CA_CHECK(!cx_allocator_counted_has_leaks(&counted));
    CA_CHECK_EQ(0, counted.live_blocks);

    ca_library_stats stats;
    CA_CHECK_EQ(CA_OK, ca_library_stats_init(&stats, &alloc));
    stats.has_top_tracks = true;
    stats.top_tracks = nullptr;
    ca_library_stats copy_stats;
    CA_CHECK_EQ(CA_OK, ca_library_stats_copy(&copy_stats, &stats, &alloc));
    ca_library_stats_destroy(&copy_stats);
    ca_library_stats_destroy(&stats);

    CA_CHECK(!cx_allocator_counted_has_leaks(&counted));
    CA_CHECK_EQ(0, counted.live_blocks);

    ca_track_query query;
    CA_CHECK_EQ(CA_OK, ca_track_query_init(&query, &alloc));
    ca_track_query copy_query;
    CA_CHECK_EQ(CA_OK, ca_track_query_copy(&copy_query, &query, &alloc));
    ca_track_query_destroy(&copy_query);
    ca_track_query_destroy(&query);

    ca_engine_event event;
    CA_CHECK_EQ(CA_OK, ca_engine_event_init(&event, &alloc));
    CA_CHECK_EQ(CX_OK, cx_string_assign(event.msg, "boom"));
    ca_engine_event copy_event;
    CA_CHECK_EQ(CA_OK, ca_engine_event_copy(&copy_event, &event, &alloc));
    ca_engine_event_destroy(&copy_event);
    ca_engine_event_destroy(&event);

    ca_playlist playlist;
    CA_CHECK_EQ(CA_OK, ca_playlist_init(&playlist, &alloc));
    CA_CHECK_EQ(CX_OK, cx_string_assign(playlist.name, "Favorites"));
    ca_playlist playlist_copy;
    CA_CHECK_EQ(CA_OK, ca_playlist_copy(&playlist_copy, &playlist, &alloc));
    ca_playlist_destroy(&playlist_copy);
    ca_playlist_destroy(&playlist);

    ca_library library;
    CA_CHECK_EQ(CA_OK, ca_library_init(&library, &alloc));
    ca_library_destroy(&library);

    CA_CHECK(!cx_allocator_counted_has_leaks(&counted));
    CA_CHECK_EQ(0, counted.live_blocks);
    cx_allocator_counted_destroy(&counted);
}

int main(void) {
    test_wire_names();
    test_track_lifecycle();
    test_normalized_source();
    test_components();
    test_invalid_arguments();
    test_leak_free();
    return ca_test_finish("types");
}
