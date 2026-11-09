#include <caudio/media/audio_ext.h>

#include "ca_test.h"

static void test_has_audio_ext(void) {
    /* Supported extensions, case-insensitive */
    CA_CHECK(ca_media_has_audio_ext("/music/song.mp3"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.MP3"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.Mp3"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.flac"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.FLAC"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.ogg"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.opus"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.oga"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.wav"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.m4a"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.mp4"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.wma"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.asf"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.aiff"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.aif"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.ape"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.mpc"));
    CA_CHECK(ca_media_has_audio_ext("/music/song.wv"));

    /* Unsupported extensions */
    CA_CHECK(!ca_media_has_audio_ext("/music/song.txt"));
    CA_CHECK(!ca_media_has_audio_ext("/music/song.pdf"));
    CA_CHECK(!ca_media_has_audio_ext("/music/song.jpg"));
    CA_CHECK(!ca_media_has_audio_ext("/music/song.mp4x"));
    /* ".mp3" has a valid .mp3 extension */
    CA_CHECK(ca_media_has_audio_ext("/music/.mp3"));

    /* NULL and empty */
    CA_CHECK(!ca_media_has_audio_ext(nullptr));
    CA_CHECK(!ca_media_has_audio_ext(""));
    CA_CHECK(!ca_media_has_audio_ext("."));
    CA_CHECK(!ca_media_has_audio_ext("/music/"));
}

int main(void) {
    test_has_audio_ext();
    return ca_test_finish("audio_ext");
}
