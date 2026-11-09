#include <caudio/media/decoder.h>
#include <caudio/media/reader.h>

#include <caudio/runtime/error.h>

#include <cstdx/allocator.h>
#include <cstdx/string.h>
#include <cstdx/string_view.h>

#include <math.h>
#include <string.h>

/* ---------------------------------------------------------- */
/* Decoder stub                                               */
/*                                                            */
/* kanariv uses FFmpeg for audio decoding. FFmpeg is not       */
/* vendored in caudio, so this is a placeholder that reports   */
/* CA_NOT_SUPPORTED for all operations. When FFmpeg is added,  */
/* implement the full decoder here.                            */
/* ---------------------------------------------------------- */

struct ca_decoder {
    uint32_t sample_rate;
    uint32_t channels;
    uint64_t total_frames;
};

bool ca_decoder_probe(cx_string_view data) {
    /* Permissive: any input of 4+ bytes is attempted */
    return data.length >= 4;
}

ca_status ca_decoder_open(ca_decoder** out,
                          ca_reader* reader,
                          ca_logger* logger,
                          ca_error* err) {
    if (err) ca_error_init(err);
    if (!out || !reader) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        return CA_INVALID_ARGUMENT;
    }
    if (!logger) logger = nullptr;

    (void)reader;
    (void)logger;

    if (err) ca_error_set(err, CA_NOT_SUPPORTED, "FFmpeg decoder not available in caudio");
    return CA_NOT_SUPPORTED;
}

uint32_t ca_decoder_sample_rate(const ca_decoder* self) {
    return self ? self->sample_rate : 0;
}

uint32_t ca_decoder_channels(const ca_decoder* self) {
    return self ? self->channels : 0;
}

uint64_t ca_decoder_total_frames(const ca_decoder* self) {
    return self ? self->total_frames : 0;
}

size_t ca_decoder_decode(ca_decoder* self, float* out, size_t out_length) {
    (void)self;
    (void)out;
    (void)out_length;
    return 0;
}

ca_status ca_decoder_seek(ca_decoder* self, double seconds, ca_error* err) {
    if (err) ca_error_init(err);
    if (!self) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "decoder is NULL");
        return CA_INVALID_ARGUMENT;
    }
    if (!isfinite(seconds) || seconds < 0.0) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "invalid seconds");
        return CA_INVALID_ARGUMENT;
    }
    if (err) ca_error_set(err, CA_NOT_SUPPORTED, "FFmpeg decoder not available");
    return CA_NOT_SUPPORTED;
}

void ca_decoder_destroy(ca_decoder* self) {
    if (!self) return;
    const cx_allocator* alloc = cx_allocator_default();
    cx_allocator_free(alloc, self, sizeof(struct ca_decoder), alignof(struct ca_decoder));
}

ca_status ca_decoder_extract_metadata(const char* path,
                                      cx_string** title,
                                      cx_string** artist,
                                      cx_string** album,
                                      cx_string** genre,
                                      double* duration,
                                      uint32_t* sample_rate,
                                      uint32_t* channels,
                                      int32_t* bitrate,
                                      ca_error* err) {
    if (err) ca_error_init(err);
    if (!path) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "path is NULL");
        return CA_INVALID_ARGUMENT;
    }
    if (title) *title = nullptr;
    if (artist) *artist = nullptr;
    if (album) *album = nullptr;
    if (genre) *genre = nullptr;
    if (duration) *duration = 0.0;
    if (sample_rate) *sample_rate = 0;
    if (channels) *channels = 0;
    if (bitrate) *bitrate = 0;

    if (err) ca_error_set(err, CA_NOT_SUPPORTED, "metadata extraction not available");
    return CA_NOT_SUPPORTED;
}
