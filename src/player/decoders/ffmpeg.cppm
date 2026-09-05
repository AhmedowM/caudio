module;
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

export module caudio.player:ffmpeg;

import caudio.utils;
import :reader;
import :decoder_interface;
import :decoder_common;

export namespace caudio::player {

class FfmpegDecoder final : public IDecoder {
  public:
    static bool probe(std::span<const std::byte> data) noexcept {
        // When FFmpeg is available, be permissive - try to decode anything
        // FFmpeg's avformat_open_input will fail gracefully if format not supported
        // Only skip obviously empty data
        return data.size() >= 4;
    }

    static caudio::utils::Expected<std::unique_ptr<IDecoder>> create(Reader &reader) {
        auto p = std::unique_ptr<FfmpegDecoder>(new FfmpegDecoder());
        p->reader_ = &reader;

        if (!p->init()) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::Unsupported, "FFmpeg init failed"});
        }

        return caudio::utils::Expected<std::unique_ptr<IDecoder>>{
            std::unique_ptr<IDecoder>(std::move(p))};
    }

    [[nodiscard]] uint32_t sampleRate() const noexcept override {
        return sampleRate_;
    }
    [[nodiscard]] uint32_t channels() const noexcept override {
        return channels_;
    }
    [[nodiscard]] uint64_t totalFrames() const noexcept override {
        return totalFrames_;
    }

    std::size_t decode(std::span<float> out) override {
        if (out.empty() || !fmt_ || !dec_ || !swr_)
            return 0;

        std::size_t frames = out.size() / channels_;
        if (frames == 0)
            return 0;

        std::size_t totalDecoded = 0;
        bool eofReached = false;
        while (totalDecoded < frames && !eofReached) {
            AVPacket *pkt = av_packet_alloc();
            if (!pkt)
                break;

            int ret = av_read_frame(fmt_, pkt);
            if (ret < 0) {
                av_packet_free(&pkt);
                // EOF: flush decoder internal buffers
                if (ret == AVERROR_EOF || ret < 0) {
                    // send flush packet
                    (void)avcodec_send_packet(dec_, nullptr);
                    // drain all remaining frames
                    while (totalDecoded < frames) {
                        AVFrame *frame = av_frame_alloc();
                        if (!frame)
                            break;
                        ret = avcodec_receive_frame(dec_, frame);
                        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                            av_frame_free(&frame);
                            break;
                        }
                        if (ret < 0) {
                            av_frame_free(&frame);
                            break;
                        }
                        uint8_t *outPtrs[1] = {
                            reinterpret_cast<uint8_t *>(out.data() + totalDecoded * channels_)};
                        int outSamples = static_cast<int>(frames - totalDecoded);
                        int converted = swr_convert(
                            swr_, outPtrs, outSamples,
                            const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
                        if (converted > 0)
                            totalDecoded += static_cast<std::size_t>(converted);
                        av_frame_free(&frame);
                        if (converted <= 0)
                            break;
                    }
                    // also flush resampler delay
                    if (totalDecoded < frames) {
                        uint8_t *outPtrs[1] = {
                            reinterpret_cast<uint8_t *>(out.data() + totalDecoded * channels_)};
                        int outSamples = static_cast<int>(frames - totalDecoded);
                        int flushed = swr_convert(swr_, outPtrs, outSamples, nullptr, 0);
                        if (flushed > 0)
                            totalDecoded += static_cast<std::size_t>(flushed);
                    }
                }
                eofReached = true;
                break;
            }

            if (pkt->stream_index != audioStreamIdx_) {
                av_packet_free(&pkt);
                continue;
            }

            ret = avcodec_send_packet(dec_, pkt);
            av_packet_free(&pkt);
            if (ret == AVERROR(EAGAIN)) {
                // need to receive before sending again
                AVFrame *frame = av_frame_alloc();
                if (frame) {
                    if (avcodec_receive_frame(dec_, frame) == 0) {
                        uint8_t *outPtrs[1] = {
                            reinterpret_cast<uint8_t *>(out.data() + totalDecoded * channels_)};
                        int outSamples = static_cast<int>(frames - totalDecoded);
                        int converted = swr_convert(
                            swr_, outPtrs, outSamples,
                            const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
                        if (converted > 0)
                            totalDecoded += static_cast<std::size_t>(converted);
                    }
                    av_frame_free(&frame);
                }
                continue;
            }
            if (ret < 0)
                continue;

            while (totalDecoded < frames) {
                AVFrame *frame = av_frame_alloc();
                if (!frame)
                    break;
                ret = avcodec_receive_frame(dec_, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    av_frame_free(&frame);
                    break;
                }
                if (ret < 0) {
                    av_frame_free(&frame);
                    break;
                }
                uint8_t *outPtrs[1] = {
                    reinterpret_cast<uint8_t *>(out.data() + totalDecoded * channels_)};
                int outSamples = static_cast<int>(frames - totalDecoded);
                int converted = swr_convert(swr_, outPtrs, outSamples,
                                            const_cast<const uint8_t **>(frame->extended_data),
                                            frame->nb_samples);
                if (converted > 0)
                    totalDecoded += static_cast<std::size_t>(converted);
                av_frame_free(&frame);
                if (converted <= 0)
                    break;
            }
        }

        pos_ += totalDecoded;
        return totalDecoded;
    }

    caudio::utils::Expected<void> seek(double seconds) override {
        if (seconds < 0.0 || !std::isfinite(seconds) || !fmt_ || !dec_ || audioStreamIdx_ < 0) {
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::InvalidArg, "bad seconds"});
        }
        AVStream *stream = fmt_->streams[audioStreamIdx_];
        if (!stream)
            return std::unexpected(
                caudio::utils::Error{caudio::utils::Result::Internal, "no stream"});
        // Use stream time_base for seeking - dec time_base is codec, not correct for container
        int64_t seekTarget = av_rescale_q(static_cast<int64_t>(seconds * AV_TIME_BASE),
                                          AVRational{1, AV_TIME_BASE}, stream->time_base);
        // Adjust by stream start_time if present
        if (stream->start_time != AV_NOPTS_VALUE)
            seekTarget += stream->start_time;
        else if (fmt_->start_time != AV_NOPTS_VALUE) {
            // fmt start_time is in AV_TIME_BASE units, convert to stream units
            seekTarget +=
                av_rescale_q(fmt_->start_time, AVRational{1, AV_TIME_BASE}, stream->time_base);
        }
        int ret = avformat_seek_file(fmt_, audioStreamIdx_, INT64_MIN, seekTarget, INT64_MAX, 0);
        if (ret < 0) {
            // Fallback to simple av_seek_frame
            ret = av_seek_frame(fmt_, audioStreamIdx_, seekTarget, AVSEEK_FLAG_BACKWARD);
            if (ret < 0)
                return std::unexpected(
                    caudio::utils::Error{caudio::utils::Result::Io, "seek failed"});
        }
        avcodec_flush_buffers(dec_);
        // flush resampler as well
        if (swr_)
            swr_init(swr_);
        pos_ = static_cast<uint64_t>(seconds * sampleRate_);
        return {};
    }

    ~FfmpegDecoder() override {
        cleanup();
    }

  private:
    FfmpegDecoder() = default;

    static int readCallback(void *opaque, uint8_t *buf, int bufSize) {
        auto *self = static_cast<FfmpegDecoder *>(opaque);
        if (!self->reader_)
            return AVERROR(EIO);
        std::span<std::byte> dst(reinterpret_cast<std::byte *>(buf), bufSize);
        std::size_t n = self->reader_->read(dst);
        return n > 0 ? static_cast<int>(n) : AVERROR_EOF;
    }

    static int64_t seekCallback(void *opaque, int64_t offset, int whence) {
        auto *self = static_cast<FfmpegDecoder *>(opaque);
        if (!self->reader_)
            return AVERROR(EIO);
        // Handle AVSEEK_SIZE (0x10000) — query file size without seeking
        if (whence & AVSEEK_SIZE) {
            int64_t sz = self->reader_->size();
            if (sz < 0)
                return AVERROR(EIO);
            return sz;
        }
        // Strip AVSEEK_FORCE flag (0x20000) if present
        int whenceMasked = whence & ~AVSEEK_FORCE;
        int w = SEEK_SET;
        if (whenceMasked == SEEK_CUR)
            w = SEEK_CUR;
        else if (whenceMasked == SEEK_END)
            w = SEEK_END;
        else if (whenceMasked == SEEK_SET)
            w = SEEK_SET;
        else {
            // Unknown whence after masking — treat low 2 bits
            int low = whenceMasked & 0x3;
            if (low == SEEK_CUR)
                w = SEEK_CUR;
            else if (low == SEEK_END)
                w = SEEK_END;
            else
                w = SEEK_SET;
        }
        auto result = self->reader_->seek(offset, w);
        if (!result.has_value())
            return AVERROR(EIO);
        return self->reader_->tell();
    }

    bool init() {
        fmt_ = avformat_alloc_context();
        if (!fmt_)
            return false;

        // Allocate AVIOContext with our callbacks — use larger buffer for high-rate FLAC/WAV
        // probing
        constexpr size_t kBufferSize = 8192;
        uint8_t *avioBuffer = static_cast<uint8_t *>(av_malloc(kBufferSize));
        if (!avioBuffer) {
            avformat_free_context(fmt_);
            fmt_ = nullptr;
            return false;
        }

        avio_ = avio_alloc_context(avioBuffer, static_cast<int>(kBufferSize), 0, this,
                                   &readCallback, nullptr, &seekCallback);
        if (!avio_) {
            av_free(avioBuffer);
            avformat_free_context(fmt_);
            fmt_ = nullptr;
            return false;
        }
        // Ensure we own buffer lifetime via AVIOContext (avio_context_free will free it)
        avio_->seekable = 1;

        fmt_->pb = avio_;
        fmt_->flags |= AVFMT_FLAG_CUSTOM_IO;

        // Silence benign probe spam like "Header missing" / "CRC mismatch" during decode —
        // real errors still surfaced via return codes; use QUIET to eliminate spam that
        // masked restart bugs (Header missing x3, CRC mismatch). Keep silent after init.
        av_log_set_level(AV_LOG_QUIET);

        // Open input (probes format)
        if (avformat_open_input(&fmt_, nullptr, nullptr, nullptr) < 0) {
            cleanup();
            return false;
        }

        if (avformat_find_stream_info(fmt_, nullptr) < 0) {
            cleanup();
            return false;
        }

        // Find audio stream
        audioStreamIdx_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (audioStreamIdx_ < 0) {
            cleanup();
            return false;
        }

        AVStream *stream = fmt_->streams[audioStreamIdx_];
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            cleanup();
            return false;
        }

        dec_ = avcodec_alloc_context3(codec);
        if (!dec_) {
            cleanup();
            return false;
        }

        if (avcodec_parameters_to_context(dec_, stream->codecpar) < 0) {
            cleanup();
            return false;
        }

        if (avcodec_open2(dec_, codec, nullptr) < 0) {
            cleanup();
            return false;
        }

        // Setup resampler to float interleaved
        swr_ = swr_alloc();
        if (!swr_) {
            cleanup();
            return false;
        }

        AVChannelLayout inLayout{};
        av_channel_layout_copy(&inLayout, &dec_->ch_layout);

        AVChannelLayout outLayout{};
        av_channel_layout_default(&outLayout, static_cast<int>(dec_->ch_layout.nb_channels));

        av_opt_set_chlayout(swr_, "in_chlayout", &inLayout, 0);
        av_opt_set_int(swr_, "in_sample_rate", dec_->sample_rate, 0);
        av_opt_set_sample_fmt(swr_, "in_sample_fmt", dec_->sample_fmt, 0);
        av_opt_set_chlayout(swr_, "out_chlayout", &outLayout, 0);
        av_opt_set_int(swr_, "out_sample_rate", dec_->sample_rate, 0);
        av_opt_set_sample_fmt(swr_, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

        if (swr_init(swr_) < 0) {
            av_channel_layout_uninit(&inLayout);
            av_channel_layout_uninit(&outLayout);
            cleanup();
            return false;
        }
        av_channel_layout_uninit(&inLayout);
        av_channel_layout_uninit(&outLayout);

        sampleRate_ = static_cast<uint32_t>(dec_->sample_rate);
        channels_ = static_cast<uint32_t>(dec_->ch_layout.nb_channels);

        // Estimate total frames from duration — try all sources
        if (fmt_->duration != AV_NOPTS_VALUE && fmt_->duration > 0) {
            totalFrames_ = static_cast<uint64_t>(fmt_->duration * sampleRate_ / AV_TIME_BASE);
        } else if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
            totalFrames_ =
                static_cast<uint64_t>(stream->duration * av_q2d(stream->time_base) * sampleRate_);
        } else if (stream->nb_frames > 0) {
            totalFrames_ = stream->nb_frames;
        } else if (stream->codecpar->bit_rate > 0 && fmt_->duration == AV_NOPTS_VALUE) {
            int64_t sz = reader_ ? reader_->size() : -1;
            if (sz > 0 && stream->codecpar->bit_rate > 0) {
                // fallback for CBR MP3 with no duration
                double secs = (sz * 8.0) / stream->codecpar->bit_rate;
                totalFrames_ = static_cast<uint64_t>(secs * sampleRate_);
            } else {
                totalFrames_ = 0;
            }
        } else {
            totalFrames_ = 0; // Unknown — will still decode to EOF
        }

        pos_ = 0;
        return true;
    }

    void cleanup() {
        if (swr_) {
            swr_free(&swr_);
            swr_ = nullptr;
        }
        if (dec_) {
            avcodec_free_context(&dec_);
            dec_ = nullptr;
        }
        // avformat_close_input must come before avio free for custom IO
        if (fmt_) {
            // prevent avformat_close_input from freeing our custom pb twice
            // it will free fmt but not our avio if AVFMT_FLAG_CUSTOM_IO is set
            AVIOContext *savedPb = fmt_->pb;
            fmt_->pb = nullptr;
            avformat_close_input(&fmt_);
            fmt_ = nullptr;
            // if we saved pb, restore to avio_ for later free (avoid double)
            if (savedPb && savedPb != avio_) {
                // shouldn't happen, but free saved if different
                avio_context_free(&savedPb);
            }
        }
        if (avio_) {
            // avio_context_free also frees the buffer allocated via av_malloc
            avio_context_free(&avio_);
            avio_ = nullptr;
        }
    }

    Reader *reader_{nullptr};
    AVFormatContext *fmt_{nullptr};
    AVCodecContext *dec_{nullptr};
    AVIOContext *avio_{nullptr};
    struct SwrContext *swr_{nullptr};
    int audioStreamIdx_{-1};
    uint32_t sampleRate_{0};
    uint32_t channels_{0};
    uint64_t totalFrames_{0};
    uint64_t pos_{0};
};

} // namespace caudio::player