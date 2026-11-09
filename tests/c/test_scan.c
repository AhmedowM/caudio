#include <caudio/media/scan.h>
#include <caudio/media/audio_ext.h>

#include <caudio/runtime/error.h>
#include <caudio/runtime/fs.h>

#include <cstdx/allocator.h>
#include <cstdx/allocator_counted.h>
#include <cstdx/string.h>
#include <cstdx/vector.h>

#include <caudio/types/record.h>

#include <stdio.h>
#include <string.h>

#include "ca_test.h"

static const char *TEST_DIR = "caudio_test_scan_dir";

static void create_test_file(const char *path, const char *content) {
    ca_error err;
    ca_error_init(&err);
    ca_file_t f = ca_fs_open(path, CA_FS_WRITE | CA_FS_CREATE | CA_FS_TRUNCATE, &err);
    CA_CHECK(f != nullptr);
    if (f) {
        ca_fs_write(f, content, strlen(content), &err);
        ca_fs_close(f);
    }
}

static int scan_count = 0;
static bool scan_callback(const ca_track *track, void *user_data) {
    (void)user_data;
    scan_count++;
    CA_CHECK(track != nullptr);
    CA_CHECK(track->path != nullptr);
    CA_CHECK(cx_string_length(track->path) > 0);
    CA_CHECK(ca_media_has_audio_ext(cx_string_cstr(track->path)));
    return true;
}

static void test_scan_tracks(void) {
    ca_error err;
    ca_error_init(&err);

    /* Create test directory with audio files */
    CA_CHECK_EQ(CA_OK, ca_fs_mkdirs(TEST_DIR, &err));

    create_test_file("caudio_test_scan_dir/song1.mp3", "fake mp3 data");
    create_test_file("caudio_test_scan_dir/song2.FLAC", "fake flac data");
    create_test_file("caudio_test_scan_dir/not_audio.txt", "not audio");
    create_test_file("caudio_test_scan_dir/song3.ogg", "fake ogg data");

    /* Subdirectory with audio */
    CA_CHECK_EQ(CA_OK, ca_fs_mkdirs("caudio_test_scan_dir/sub", &err));
    create_test_file("caudio_test_scan_dir/sub/song4.wav", "fake wav data");

    scan_count = 0;
    CA_CHECK_EQ(CA_OK, ca_media_scan_tracks(TEST_DIR, CA_MEDIA_SCAN_SAMPLED,
                                              scan_callback, NULL, &err));
    /* 4 audio files found (song1.mp3, song2.FLAC, song3.ogg, sub/song4.wav) */
    CA_CHECK_EQ(4, scan_count);

    /* Clean up */
    ca_fs_remove("caudio_test_scan_dir/song1.mp3", &err);
    ca_fs_remove("caudio_test_scan_dir/song2.FLAC", &err);
    ca_fs_remove("caudio_test_scan_dir/not_audio.txt", &err);
    ca_fs_remove("caudio_test_scan_dir/song3.ogg", &err);
    ca_fs_remove("caudio_test_scan_dir/sub/song4.wav", &err);
    ca_fs_remove("caudio_test_scan_dir/sub", &err);
    ca_fs_remove(TEST_DIR, &err);
}

static void test_scan_tracks_to_vector(void) {
    ca_error err;
    ca_error_init(&err);
    const cx_allocator* alloc = cx_allocator_default();

    CA_CHECK_EQ(CA_OK, ca_fs_mkdirs(TEST_DIR, &err));
    create_test_file("caudio_test_scan_dir/track1.mp3", "audio 1");
    create_test_file("caudio_test_scan_dir/track2.flac", "audio 2");

    cx_vector *tracks = nullptr;
    CA_CHECK_EQ(CA_OK, ca_media_scan_tracks_to_vector(TEST_DIR, CA_MEDIA_SCAN_SAMPLED,
                                                       &tracks, alloc, &err));
    CA_CHECK(tracks != nullptr);
    CA_CHECK_EQ(2, cx_vector_length(tracks));

    /* Verify first track */
    const ca_track *t1 = (const ca_track *)cx_vector_ptr(tracks, 0);
    CA_CHECK(t1 != nullptr);
    CA_CHECK(t1->path != nullptr);
    CA_CHECK(ca_media_has_audio_ext(cx_string_cstr(t1->path)));

    /* Verify second track */
    const ca_track *t2 = (const ca_track *)cx_vector_ptr(tracks, 1);
    CA_CHECK(t2 != nullptr);
    CA_CHECK(t2->path != nullptr);
    CA_CHECK(ca_media_has_audio_ext(cx_string_cstr(t2->path)));

    /* Clean up */
    cx_vector_destroy(&tracks);
    ca_fs_remove("caudio_test_scan_dir/track1.mp3", &err);
    ca_fs_remove("caudio_test_scan_dir/track2.flac", &err);
    ca_fs_remove(TEST_DIR, &err);
}

static void test_scan_nonexistent(void) {
    ca_error err;
    ca_error_init(&err);
    CA_CHECK_EQ(CA_NOT_FOUND,
                ca_media_scan_tracks("caudio_nonexistent_scan_dir_xyz",
                                      CA_MEDIA_SCAN_SAMPLED, scan_callback, NULL, &err));
}

int main(void) {
    test_scan_tracks();
    test_scan_tracks_to_vector();
    test_scan_nonexistent();
    return ca_test_finish("scan");
}
