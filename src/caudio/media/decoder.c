#include <caudio/media/decoder.h>
#include <caudio/media/reader.h>

#include <caudio/runtime/error.h>
#include <caudio/runtime/log.h>

#include <cstdx/allocator.h>
#include <cstdx/string.h>
#include <cstdx/string_view.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

/* ---------------------------------------------------------- */
/* FFmpeg decoder implementation                              */
/* ---------------------------------------------------------- */

static constexpr size_t AUDIO_IO_BUFFER_SIZE = 8192;
    static constexpr size_t CONVERT_FRAMES = 4096;

static_assert(AUDIO_IO_BUFFER_SIZE > 0, "AUDIO_IO_BUFFER_SIZE must be positive");
static_assert(CONVERT_FRAMES > 0, "CONVERT_FRAMES must be positive");
static_assert((CONVERT_FRAMES & (CONVERT_FRAMES - 1)) == 0,
              "CONVERT_FRAMES must be a power of two");

struct ca_decoder {
    ca_reader* reader;
    ca_logger* logger;

    AVFormatContext* fmt_ctx;
    AVCodecContext* dec_ctx;
    AVIOContext* avio_ctx;
    SwrContext* swr_ctx;
    int audio_stream_idx;

    uint32_t sample_rate;
    uint32_t channels;
    uint64_t total_frames;

    uint8_t* io_buffer;
    size_t io_buffer_size;

    AVPacket* pkt;
    AVFrame* frame;

    float* convert_buf;
    size_t convert_buf_size;
    float* spill_buf;
    size_t spill_size;
    size_t spill_pos;
};

/* ---------------------------------------------------------- */
/* AVIO callbacks bridging ca_reader to FFmpeg                */
/* ---------------------------------------------------------- */

static int decoder_read_cb(void* opaque, uint8_t* buf, int buf_size) {
    ca_decoder* self = (ca_decoder*)opaque;
    if (!self || !self->reader)
        return AVERROR(EIO);

    size_t n = ca_reader_read(self->reader, buf, (size_t)buf_size);
    if (n == 0)
        return AVERROR_EOF;
    return (int)n;
}

static int64_t decoder_seek_cb(void* opaque, int64_t offset, int whence) {
    ca_decoder* self = (ca_decoder*)opaque;
    if (!self || !self->reader || !self->reader->ops->seekable(self->reader))
        return AVERROR(EIO);

    /* AVSEEK_SIZE queries file size */
    if (whence & AVSEEK_SIZE) {
        int64_t sz = ca_reader_size(self->reader);
        if (sz < 0)
            return AVERROR(EIO);
        return sz;
    }

    /* Strip AVSEEK_FORCE flag */
    int whence_masked = whence & ~AVSEEK_FORCE;
    int w = SEEK_SET;
    if (whence_masked == SEEK_CUR)
        w = SEEK_CUR;
    else if (whence_masked == SEEK_END)
        w = SEEK_END;
    else if (whence_masked == SEEK_SET)
        w = SEEK_SET;

    ca_status st = ca_reader_seek(self->reader, offset, w);
    if (st != CA_OK)
        return AVERROR(EIO);

    int64_t pos = ca_reader_tell(self->reader);
    if (pos < 0)
        return AVERROR(EIO);
    return pos;
}

/* ---------------------------------------------------------- */
/* Resampler helpers                                          */
/* ---------------------------------------------------------- */

static int convert_frame_buffered(ca_decoder* self, AVFrame* frame) {
    if (!self || !self->swr_ctx || !frame || frame->nb_samples <= 0)
        return 0;

    int ch = (int)self->channels;
    size_t needed = (size_t)CONVERT_FRAMES * (size_t)ch * sizeof(float);
    if (self->convert_buf_size < needed) {
        float* new_buf = (float*)cx_allocator_realloc(
            cx_allocator_default(),
            self->convert_buf,
            self->convert_buf_size,
            needed,
            alignof(float));
        if (!new_buf)
            return -1;
        self->convert_buf = new_buf;
        self->convert_buf_size = needed;
    }

    uint8_t* out_ptrs[1] = { (uint8_t*)self->convert_buf };
    int n = swr_convert(self->swr_ctx,
                        out_ptrs,
                        (int)CONVERT_FRAMES,
                        (const uint8_t**)frame->extended_data,
                        frame->nb_samples);
    if (n < 0)
        return n;

    /* Append to spill buffer */
    size_t spill_needed = self->spill_size + (size_t)n * self->channels * sizeof(float);
    float* new_spill = (float*)cx_allocator_realloc(
        cx_allocator_default(),
        self->spill_buf,
        self->spill_size,
        spill_needed,
        alignof(float));
    if (!new_spill)
        return -1;
    self->spill_buf = new_spill;
    self->spill_size = spill_needed;
    memcpy(self->spill_buf + (self->spill_size - (size_t)n * self->channels * sizeof(float)),
           self->convert_buf,
           (size_t)n * self->channels * sizeof(float));
    return n;
}

static int drain_resampler(ca_decoder* self) {
    if (!self->swr_ctx || self->channels == 0)
        return 0;

    uint8_t* out_ptrs[1] = { (uint8_t*)self->convert_buf };
    int appended = 0;
    int n;
    while ((n = swr_convert(self->swr_ctx,
                            out_ptrs,
                            (int)CONVERT_FRAMES,
                            nullptr,
                            0)) > 0) {
        size_t spill_needed = self->spill_size + (size_t)n * self->channels * sizeof(float);
        float* new_spill = (float*)cx_allocator_realloc(
            cx_allocator_default(),
            self->spill_buf,
            self->spill_size,
            spill_needed,
            alignof(float));
        if (!new_spill)
            return n < 0 ? n : appended;
        self->spill_buf = new_spill;
        self->spill_size = spill_needed;
        memcpy(self->spill_buf + (self->spill_size - (size_t)n * self->channels * sizeof(float)),
               self->convert_buf,
               (size_t)n * self->channels * sizeof(float));
        appended += n;
    }
    return n < 0 ? n : appended;
}

static size_t serve_spill(ca_decoder* self,
                          float* out,
                          size_t total_decoded,
                          size_t frames) {
    if (self->spill_pos >= self->spill_size || self->channels == 0)
        return 0;

    size_t avail_frames = (self->spill_size - self->spill_pos) / (self->channels * sizeof(float));
    size_t want = frames - total_decoded;
    size_t n = (avail_frames < want) ? avail_frames : want;
    if (n == 0)
        return 0;

    memcpy(out + total_decoded * self->channels,
           self->spill_buf + self->spill_pos,
           n * self->channels * sizeof(float));
    self->spill_pos += n * self->channels * sizeof(float);
    if (self->spill_pos == self->spill_size) {
        cx_allocator_free(cx_allocator_default(),
                          self->spill_buf,
                          self->spill_size,
                          alignof(float));
        self->spill_buf = nullptr;
        self->spill_size = 0;
        self->spill_pos = 0;
    }
    return n;
}

/* ---------------------------------------------------------- */
/* ca_decoder_probe                                           */
/* ---------------------------------------------------------- */

bool ca_decoder_probe(cx_string_view data) {
    /* Permissive: any input of 4+ bytes is attempted */
    return data.length >= 4;
}

/* ---------------------------------------------------------- */
/* ca_decoder_open                                            */
/* ---------------------------------------------------------- */

ca_status ca_decoder_open(ca_decoder** out,
                          ca_reader* reader,
                          ca_logger* logger,
                          ca_error* err) {
    if (err) ca_error_init(err);
    if (!out || !reader) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "invalid arguments");
        return CA_INVALID_ARGUMENT;
    }

    ca_decoder* self = (ca_decoder*)cx_allocator_alloc(
        cx_allocator_default(),
        sizeof(ca_decoder),
        alignof(ca_decoder));
    if (!self) {
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }

    memset(self, 0, sizeof(ca_decoder));
    self->reader = reader;
    self->logger = logger;
    self->io_buffer = nullptr;
    self->io_buffer_size = 0;
    self->pkt = nullptr;
    self->frame = nullptr;
    self->convert_buf = nullptr;
    self->convert_buf_size = 0;
    self->spill_buf = nullptr;
    self->spill_size = 0;
    self->spill_pos = 0;

    /* Allocate AVIO buffer */
    self->io_buffer = (uint8_t*)av_malloc(AUDIO_IO_BUFFER_SIZE);
    if (!self->io_buffer) {
        ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        ca_decoder_destroy(self);
        *out = nullptr;
        return CA_OUT_OF_MEMORY;
    }
    self->io_buffer_size = AUDIO_IO_BUFFER_SIZE;

    /* Allocate AVIO context */
    self->avio_ctx = avio_alloc_context(self->io_buffer,
                                        (int)self->io_buffer_size,
                                        0,   /* write_flag = 0 (read only) */
                                        self,   /* opaque */
                                        decoder_read_cb,
                                        nullptr,   /* write_cb */
                                        self->reader->ops->seekable(self->reader) ? decoder_seek_cb : nullptr);
    if (!self->avio_ctx) {
        ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        av_free(self->io_buffer);
        self->io_buffer = nullptr;
        ca_decoder_destroy(self);
        *out = nullptr;
        return CA_OUT_OF_MEMORY;
    }

    /* Allocate AVFormatContext */
    self->fmt_ctx = avformat_alloc_context();
    if (!self->fmt_ctx) {
        ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        ca_decoder_destroy(self);
        *out = nullptr;
        return CA_OUT_OF_MEMORY;
    }

    self->fmt_ctx->pb = self->avio_ctx;
    self->fmt_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;

    /* Non-seekable sources must not advertise seeking */
    self->avio_ctx->seekable = self->reader->ops->seekable(self->reader) ? 1 : 0;

    /* Open input */
    if (avformat_open_input(&self->fmt_ctx, nullptr, nullptr, nullptr) < 0) {
        ca_error_set(err, CA_NOT_SUPPORTED, "avformat_open_input failed");
        ca_decoder_destroy(self);
        *out = nullptr;
        return CA_NOT_SUPPORTED;
    }

    /* Bound probing */
    self->fmt_ctx->probesize = 1048576;
    self->fmt_ctx->max_analyze_duration = 500000;

    if (avformat_find_stream_info(self->fmt_ctx, nullptr) < 0) {
        ca_error_set(err, CA_IO_ERROR, "avformat_find_stream_info failed");
        ca_decoder_destroy(self);
        *out = nullptr;
        return CA_IO_ERROR;
    }

    /* Find audio stream */
    self->audio_stream_idx = av_find_best_stream(self->fmt_ctx,
                                                  AVMEDIA_TYPE_AUDIO,
                                                  -1, -1, nullptr, 0);
    if (self->audio_stream_idx < 0) {
        ca_error_set(err, CA_NOT_SUPPORTED, "no audio stream found");
        ca_decoder_destroy(self);
        *out = nullptr;
        return CA_NOT_SUPPORTED;
    }

    AVStream* stream = self->fmt_ctx->streams[self->audio_stream_idx];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        ca_error_set(err, CA_NOT_SUPPORTED, "no decoder found");
        ca_decoder_destroy(self);
        *out = nullptr;
        return CA_NOT_SUPPORTED;
    }

    self->dec_ctx = avcodec_alloc_context3(codec);
    if (!self->dec_ctx) {
        ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        ca_decoder_destroy(self);
        *out = nullptr;
        return CA_OUT_OF_MEMORY;
    }

    if (avcodec_parameters_to_context(self->dec_ctx, stream->codecpar) < 0) {
        ca_error_set(err, CA_IO_ERROR, "avcodec_parameters_to_context failed");
        ca_decoder_destroy(self);
        *out = nullptr;
        return CA_IO_ERROR;
    }

    if (avcodec_open2(self->dec_ctx, codec, nullptr) < 0) {
        ca_error_set(err, CA_IO_ERROR, "avcodec_open2 failed");
        ca_decoder_destroy(self);
        *out = nullptr;
        return CA_IO_ERROR;
    }

    /* Setup resampler to float interleaved */
    self->swr_ctx = swr_alloc();
    if (!self->swr_ctx) {
        ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        ca_decoder_destroy(self);
        *out = nullptr;
        return CA_OUT_OF_MEMORY;
    }

    AVChannelLayout in_layout;
    AVChannelLayout out_layout;
    av_channel_layout_uninit(&in_layout);
    av_channel_layout_uninit(&out_layout);
    av_channel_layout_copy(&in_layout, &self->dec_ctx->ch_layout);
    av_channel_layout_copy(&out_layout, &self->dec_ctx->ch_layout);

    av_opt_set_chlayout(self->swr_ctx, "in_chlayout", &in_layout, 0);
    av_opt_set_int(self->swr_ctx, "in_sample_rate", self->dec_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(self->swr_ctx, "in_sample_fmt", self->dec_ctx->sample_fmt, 0);
    av_opt_set_chlayout(self->swr_ctx, "out_chlayout", &out_layout, 0);
    av_opt_set_int(self->swr_ctx, "out_sample_rate", self->dec_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(self->swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

    if (swr_init(self->swr_ctx) < 0) {
        ca_error_set(err, CA_IO_ERROR, "swr_init failed");
        ca_decoder_destroy(self);
        *out = nullptr;
        return CA_IO_ERROR;
    }

    /* Pre-allocate scratch packet/frame */
    self->pkt = av_packet_alloc();
    self->frame = av_frame_alloc();
    if (!self->pkt || !self->frame) {
        ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        ca_decoder_destroy(self);
        *out = nullptr;
        return CA_OUT_OF_MEMORY;
    }

    self->sample_rate = (uint32_t)self->dec_ctx->sample_rate;
    self->channels = (uint32_t)self->dec_ctx->ch_layout.nb_channels;

    /* Estimate total frames */
    if (self->fmt_ctx->duration != AV_NOPTS_VALUE && self->fmt_ctx->duration > 0) {
        self->total_frames = (uint64_t)(self->fmt_ctx->duration * self->sample_rate / AV_TIME_BASE);
    } else if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
        self->total_frames = (uint64_t)(stream->duration * av_q2d(stream->time_base) * self->sample_rate);
    } else {
        self->total_frames = 0;
    }

    *out = self;
    return CA_OK;
}

/* ---------------------------------------------------------- */
/* ca_decoder_decode                                          */
/* ---------------------------------------------------------- */

[[nodiscard]] CAUDIO_API uint32_t ca_decoder_sample_rate(const ca_decoder* self) {
    if (!self)
        return 0;
    return self->sample_rate;
}

[[nodiscard]] CAUDIO_API uint32_t ca_decoder_channels(const ca_decoder* self) {
    if (!self)
        return 0;
    return self->channels;
}

size_t ca_decoder_decode(ca_decoder* self,
                         float* out,
                         size_t out_length) {
    if (!self || !out || out_length == 0)
        return 0;
    if (!self->fmt_ctx || !self->dec_ctx || !self->swr_ctx || self->channels == 0)
        return 0;

    size_t frames = out_length / self->channels;
    if (frames == 0)
        return 0;

    size_t total_decoded = 0;
    bool eof_reached = false;

    if (!self->pkt)
        self->pkt = av_packet_alloc();
    if (!self->frame)
        self->frame = av_frame_alloc();
    if (!self->pkt || !self->frame)
        return 0;

    AVPacket* pkt = self->pkt;
    AVFrame* frame = self->frame;

    while (total_decoded < frames && !eof_reached) {
        total_decoded += serve_spill(self, out, total_decoded, frames);
        if (total_decoded >= frames)
            break;

        av_packet_unref(pkt);

        int ret = av_read_frame(self->fmt_ctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF || ret < 0) {
                (void)avcodec_send_packet(self->dec_ctx, nullptr);
                for (;;) {
                    av_frame_unref(frame);
                    ret = avcodec_receive_frame(self->dec_ctx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        break;
                    if (ret < 0)
                        break;
                    if (convert_frame_buffered(self, frame) < 0)
                        break;
                }
                if (drain_resampler(self) < 0)
                    break;
                total_decoded += serve_spill(self, out, total_decoded, frames);
            }
            eof_reached = true;
            break;
        }

        if (pkt->stream_index != self->audio_stream_idx) {
            continue;
        }

        ret = avcodec_send_packet(self->dec_ctx, pkt);
        if (ret == AVERROR(EAGAIN)) {
            av_frame_unref(frame);
            if (avcodec_receive_frame(self->dec_ctx, frame) == 0)
                (void)convert_frame_buffered(self, frame);
            continue;
        }
        if (ret < 0)
            continue;

        av_frame_unref(frame);
        ret = avcodec_receive_frame(self->dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            continue;
        if (ret < 0)
            continue;
        if (convert_frame_buffered(self, frame) < 0)
            break;
    }

    return total_decoded;
}

/* ---------------------------------------------------------- */
/* ca_decoder_seek                                            */
/* ---------------------------------------------------------- */

ca_status ca_decoder_seek(ca_decoder* self,
                          double seconds,
                          ca_error* err) {
    if (err) ca_error_init(err);
    if (!self) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "decoder is nullptr");
        return CA_INVALID_ARGUMENT;
    }
    if (!isfinite(seconds) || seconds < 0.0) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "invalid seconds");
        return CA_INVALID_ARGUMENT;
    }
    if (!self->fmt_ctx || !self->dec_ctx || self->audio_stream_idx < 0) {
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "decoder not initialized");
        return CA_INVALID_ARGUMENT;
    }
    if (!ca_reader_seekable(self->reader)) {
        if (err) ca_error_set(err, CA_NOT_SUPPORTED, "source is not seekable");
        return CA_NOT_SUPPORTED;
    }

    AVStream* stream = self->fmt_ctx->streams[self->audio_stream_idx];
    if (!stream) {
        if (err) ca_error_set(err, CA_IO_ERROR, "no stream");
        return CA_IO_ERROR;
    }

    int64_t seek_target = av_rescale_q((int64_t)(seconds * AV_TIME_BASE),
                                       av_make_q(1, AV_TIME_BASE),
                                       stream->time_base);
    if (stream->start_time != AV_NOPTS_VALUE)
        seek_target += stream->start_time;
    else if (self->fmt_ctx->start_time != AV_NOPTS_VALUE) {
        seek_target += av_rescale_q(self->fmt_ctx->start_time,
                                    av_make_q(1, AV_TIME_BASE),
                                    stream->time_base);
    }

    int ret = avformat_seek_file(self->fmt_ctx,
                                 self->audio_stream_idx,
                                 INT64_MIN,
                                 seek_target,
                                 INT64_MAX,
                                 0);
    if (ret < 0) {
        ret = av_seek_frame(self->fmt_ctx,
                            self->audio_stream_idx,
                            seek_target,
                            AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            if (err) ca_error_set(err, CA_IO_ERROR, "seek failed");
            return CA_IO_ERROR;
        }
    }

    avcodec_flush_buffers(self->dec_ctx);
    swr_init(self->swr_ctx);
    self->spill_size = 0;
    self->spill_pos = 0;
    self->spill_buf = nullptr;
    self->total_frames = (uint64_t)(seconds * self->sample_rate);
    return CA_OK;
}

/* ---------------------------------------------------------- */
/* ca_decoder_destroy                                         */
/* ---------------------------------------------------------- */

void ca_decoder_destroy(ca_decoder* self) {
    if (!self)
        return;

    if (self->pkt)
        av_packet_free(&self->pkt);
    if (self->frame)
        av_frame_free(&self->frame);
    if (self->swr_ctx)
        swr_free(&self->swr_ctx);
    if (self->dec_ctx)
        avcodec_free_context(&self->dec_ctx);
    if (self->fmt_ctx) {
        self->fmt_ctx->pb = nullptr;
        avformat_close_input(&self->fmt_ctx);
        self->fmt_ctx = nullptr;
    }
    if (self->avio_ctx)
        avio_context_free(&self->avio_ctx);
    if (self->io_buffer)
        av_free(self->io_buffer);
    if (self->convert_buf)
        cx_allocator_free(cx_allocator_default(),
                          self->convert_buf,
                          self->convert_buf_size,
                          alignof(float));
    if (self->spill_buf)
        cx_allocator_free(cx_allocator_default(),
                          self->spill_buf,
                          self->spill_size,
                          alignof(float));

    cx_allocator_free(cx_allocator_default(),
                      self,
                      sizeof(ca_decoder),
                      alignof(ca_decoder));
}

/* ---------------------------------------------------------- */
/* Status conversion                                          */
/* ---------------------------------------------------------- */

static ca_status ca_from_cx(cx_status status) {
    switch (status) {
    case CX_OK:             return CA_OK;
    case CX_OUT_OF_MEMORY:  return CA_OUT_OF_MEMORY;
    case CX_INVALID_ARGUMENT: return CA_INVALID_ARGUMENT;
    case CX_OVERFLOW:       return CA_OVERFLOW;
    case CX_OUT_OF_RANGE:   return CA_INVALID_ARGUMENT;
    default:                return CA_INVALID_ARGUMENT;
    }
}

/* ---------------------------------------------------------- */
/* ca_decoder_extract_metadata                                */
/* ---------------------------------------------------------- */

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
        if (err) ca_error_set(err, CA_INVALID_ARGUMENT, "path is nullptr");
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

    AVFormatContext* fmt = avformat_alloc_context();
    if (!fmt) {
        if (err) ca_error_set(err, CA_OUT_OF_MEMORY, "out of memory");
        return CA_OUT_OF_MEMORY;
    }

    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0) {
        avformat_free_context(fmt);
        if (err) ca_error_set(err, CA_IO_ERROR, "avformat_open_input failed");
        return CA_IO_ERROR;
    }

    fmt->probesize = 1048576;
    fmt->max_analyze_duration = 500000;

    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        if (err) ca_error_set(err, CA_IO_ERROR, "avformat_find_stream_info failed");
        return CA_IO_ERROR;
    }

    int audio_stream_idx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    AVDictionary* stream_meta = (audio_stream_idx >= 0)
        ? fmt->streams[audio_stream_idx]->metadata
        : nullptr;

    if (title) {
        AVDictionaryEntry* e = av_dict_get(fmt->metadata, "title", nullptr, 0);
        if (!e || !e->value || !*e->value)
            e = av_dict_get(stream_meta, "title", nullptr, 0);
        if (e && e->value && *e->value) {
            *title = nullptr;
            cx_status cst = cx_string_init(title, cx_allocator_default());
            if (cst == CX_OK)
                ca_from_cx(cx_string_assign_view(*title, cx_string_view_from_cstr(e->value)));
        }
    }
    if (artist) {
        AVDictionaryEntry* e = av_dict_get(fmt->metadata, "artist", nullptr, 0);
        if (!e || !e->value || !*e->value)
            e = av_dict_get(stream_meta, "artist", nullptr, 0);
        if (e && e->value && *e->value) {
            *artist = nullptr;
            cx_status cst = cx_string_init(artist, cx_allocator_default());
            if (cst == CX_OK)
                ca_from_cx(cx_string_assign_view(*artist, cx_string_view_from_cstr(e->value)));
        }
    }
    if (album) {
        AVDictionaryEntry* e = av_dict_get(fmt->metadata, "album", nullptr, 0);
        if (!e || !e->value || !*e->value)
            e = av_dict_get(stream_meta, "album", nullptr, 0);
        if (e && e->value && *e->value) {
            *album = nullptr;
            cx_status cst = cx_string_init(album, cx_allocator_default());
            if (cst == CX_OK)
                ca_from_cx(cx_string_assign_view(*album, cx_string_view_from_cstr(e->value)));
        }
    }
    if (genre) {
        AVDictionaryEntry* e = av_dict_get(fmt->metadata, "genre", nullptr, 0);
        if (!e || !e->value || !*e->value)
            e = av_dict_get(stream_meta, "genre", nullptr, 0);
        if (e && e->value && *e->value) {
            *genre = nullptr;
            cx_status cst = cx_string_init(genre, cx_allocator_default());
            if (cst == CX_OK)
                ca_from_cx(cx_string_assign_view(*genre, cx_string_view_from_cstr(e->value)));
        }
    }

    if (audio_stream_idx >= 0) {
        AVStream* stream = fmt->streams[audio_stream_idx];
        if (stream->codecpar) {
            if (sample_rate) *sample_rate = (uint32_t)stream->codecpar->sample_rate;
            if (channels) *channels = (uint32_t)stream->codecpar->ch_layout.nb_channels;
            if (bitrate) *bitrate = (int32_t)stream->codecpar->bit_rate;
        }
        if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0) {
            if (duration) *duration = (double)fmt->duration / AV_TIME_BASE;
        } else if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
            if (duration) *duration = stream->duration * av_q2d(stream->time_base);
        }
    }

    avformat_close_input(&fmt);
    return CA_OK;
}
