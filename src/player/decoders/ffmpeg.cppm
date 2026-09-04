module;
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <span>
#include <memory>
#include <expected>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
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

  static caudio::utils::Expected<std::unique_ptr<IDecoder>> create(Reader& reader) {
    auto p = std::unique_ptr<FfmpegDecoder>(new FfmpegDecoder());
    p->reader_ = &reader;

    if (!p->init()) {
      return std::unexpected(caudio::utils::Error{caudio::utils::Result::Unsupported, "FFmpeg init failed"});
    }

    return caudio::utils::Expected<std::unique_ptr<IDecoder>>{std::unique_ptr<IDecoder>(std::move(p))};
  }

  [[nodiscard]] uint32_t sampleRate() const noexcept override { return sampleRate_; }
  [[nodiscard]] uint32_t channels() const noexcept override { return channels_; }
  [[nodiscard]] uint64_t totalFrames() const noexcept override { return totalFrames_; }

  std::size_t decode(std::span<float> out) override {
    if (out.empty() || !fmt_ || !dec_ || !swr_) return 0;

    std::size_t frames = out.size() / channels_;
    if (frames == 0) return 0;

    std::size_t totalDecoded = 0;
    while (totalDecoded < frames) {
      AVPacket* pkt = av_packet_alloc();
      if (!pkt) break;

      int ret = av_read_frame(fmt_, pkt);
      if (ret < 0) {
        av_packet_free(&pkt);
        break; // EOF or error
      }

      if (pkt->stream_index == audioStreamIdx_) {
        ret = avcodec_send_packet(dec_, pkt);
        av_packet_free(&pkt);
        if (ret < 0) continue;

        while (totalDecoded < frames) {
          AVFrame* frame = av_frame_alloc();
          if (!frame) break;

          ret = avcodec_receive_frame(dec_, frame);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&frame);
            break;
          }
          if (ret < 0) {
            av_frame_free(&frame);
            break;
          }

          // Convert to float planar/interleaved via swr
          uint8_t* outPtrs[1] = {reinterpret_cast<uint8_t*>(out.data() + totalDecoded * channels_)};
          int outSamples = frames - totalDecoded;
          int converted = swr_convert(swr_, outPtrs, outSamples,
                                      const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
          if (converted > 0) {
            totalDecoded += static_cast<std::size_t>(converted);
          }
          av_frame_free(&frame);
          if (converted <= 0) break;
        }
      } else {
        av_packet_free(&pkt);
      }
    }

    pos_ += totalDecoded;
    return totalDecoded;
  }

  caudio::utils::Expected<void> seek(double seconds) override {
    if (seconds < 0.0 || !std::isfinite(seconds) || !fmt_ || !dec_) {
      return std::unexpected(caudio::utils::Error{caudio::utils::Result::InvalidArg, "bad seconds"});
    }

    int64_t targetPts = static_cast<int64_t>(seconds * av_q2d(dec_->time_base) * AV_TIME_BASE);
    int64_t streamStart = fmt_->start_time != AV_NOPTS_VALUE ? fmt_->start_time : 0;
    int64_t seekTarget = streamStart + targetPts;

    int ret = avformat_seek_file(fmt_, audioStreamIdx_, INT64_MIN, seekTarget, INT64_MAX, 0);
    if (ret < 0) {
      return std::unexpected(caudio::utils::Error{caudio::utils::Result::Io, "seek failed"});
    }

    avcodec_flush_buffers(dec_);
    pos_ = static_cast<uint64_t>(seconds * sampleRate_);
    return {};
  }

  ~FfmpegDecoder() override {
    cleanup();
  }

private:
  FfmpegDecoder() = default;

  static int readCallback(void* opaque, uint8_t* buf, int bufSize) {
    auto* self = static_cast<FfmpegDecoder*>(opaque);
    if (!self->reader_) return AVERROR(EIO);
    std::span<std::byte> dst(reinterpret_cast<std::byte*>(buf), bufSize);
    std::size_t n = self->reader_->read(dst);
    return n > 0 ? static_cast<int>(n) : AVERROR_EOF;
  }

  static int64_t seekCallback(void* opaque, int64_t offset, int whence) {
    auto* self = static_cast<FfmpegDecoder*>(opaque);
    if (!self->reader_) return AVERROR(EIO);
    int w = SEEK_SET;
    if (whence == AVSEEK_FORCE) w = SEEK_SET; // AVSEEK_FORCE is a flag, not a whence
    // FFmpeg uses standard SEEK_SET/SEEK_CUR/SEEK_END for whence in avio_alloc_context
    // But the callback receives AVSEEK_* constants, map them:
    // AVSEEK_SET=0, AVSEEK_CUR=1, AVSEEK_END=2, AVSEEK_SIZE=0x10000
    if (whence == 1) w = SEEK_CUR;
    else if (whence == 2) w = SEEK_END;
    auto result = self->reader_->seek(offset, w);
    if (!result.has_value()) return AVERROR(EIO);
    return self->reader_->tell();
  }

  bool init() {
    fmt_ = avformat_alloc_context();
    if (!fmt_) return false;

    // Allocate AVIOContext with our callbacks
    constexpr size_t kBufferSize = 4096;
    uint8_t* avioBuffer = static_cast<uint8_t*>(av_malloc(kBufferSize));
    if (!avioBuffer) {
      avformat_free_context(fmt_);
      fmt_ = nullptr;
      return false;
    }

    avio_ = avio_alloc_context(avioBuffer, kBufferSize, 0, this, &readCallback, nullptr, &seekCallback);
    if (!avio_) {
      av_free(avioBuffer);
      avformat_free_context(fmt_);
      fmt_ = nullptr;
      return false;
    }

    fmt_->pb = avio_;
    fmt_->flags |= AVFMT_FLAG_CUSTOM_IO;

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

    AVStream* stream = fmt_->streams[audioStreamIdx_];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
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

    AVChannelLayout inLayout;
    av_channel_layout_default(&inLayout, dec_->ch_layout.nb_channels);
    av_channel_layout_copy(&inLayout, &dec_->ch_layout);

    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, dec_->ch_layout.nb_channels);

    av_opt_set_chlayout(swr_, "in_chlayout", &inLayout, 0);
    av_opt_set_int(swr_, "in_sample_rate", dec_->sample_rate, 0);
    av_opt_set_sample_fmt(swr_, "in_sample_fmt", dec_->sample_fmt, 0);
    av_opt_set_chlayout(swr_, "out_chlayout", &outLayout, 0);
    av_opt_set_int(swr_, "out_sample_rate", dec_->sample_rate, 0);
    av_opt_set_sample_fmt(swr_, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

    if (swr_init(swr_) < 0) {
      cleanup();
      return false;
    }

    sampleRate_ = static_cast<uint32_t>(dec_->sample_rate);
    channels_ = static_cast<uint32_t>(dec_->ch_layout.nb_channels);

    // Estimate total frames from duration
    if (fmt_->duration != AV_NOPTS_VALUE) {
      totalFrames_ = static_cast<uint64_t>(fmt_->duration * sampleRate_ / AV_TIME_BASE);
    } else if (stream->duration != AV_NOPTS_VALUE) {
      totalFrames_ = static_cast<uint64_t>(stream->duration * av_q2d(stream->time_base) * sampleRate_);
    } else {
      totalFrames_ = 0; // Unknown
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
    if (avio_) {
      if (avio_->buffer) av_free(avio_->buffer);
      avio_context_free(&avio_);
      avio_ = nullptr;
    }
    if (fmt_) {
      avformat_close_input(&fmt_);
      fmt_ = nullptr;
    }
  }

  Reader* reader_{nullptr};
  AVFormatContext* fmt_{nullptr};
  AVCodecContext* dec_{nullptr};
  AVIOContext* avio_{nullptr};
  struct SwrContext* swr_{nullptr};
  int audioStreamIdx_{-1};
  uint32_t sampleRate_{0};
  uint32_t channels_{0};
  uint64_t totalFrames_{0};
  uint64_t pos_{0};
};

} // namespace caudio::player