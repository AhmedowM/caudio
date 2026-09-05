module;
#include <miniaudio.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <span>
#include <memory>
#include <expected>
#include <string_view>
#include <array>
#include <vector>

export module caudio.player:miniaudio_decoder;

import caudio.utils;
import :reader;
import :decoder_interface;
import :decoder_common;

extern ma_result caudio_miniaudio_decoder_init_with_tell(ma_decoder_read_proc onRead,
                                                         ma_decoder_seek_proc onSeek,
                                                         ma_decoder_tell_proc onTell,
                                                         void* pUserData,
                                                         const ma_decoder_config* pConfig,
                                                         ma_decoder* pDecoder);

namespace caudio::player::detail {

enum class DecoderFormat : uint8_t {
  Unknown = 0,
  Wav = 1,
  Flac = 2,
  Mp3 = 3,
  Vorbis = 4
};

inline detail::DecoderFormat detectFormat(std::span<const std::byte> data) noexcept {
  if (data.size() < 4) return detail::DecoderFormat::Unknown;
  const char* hdr = reinterpret_cast<const char*>(data.data());
  if (std::strncmp(hdr, "RIFF", 4) == 0) return detail::DecoderFormat::Wav;
  if (std::strncmp(hdr, "fLaC", 4) == 0) return detail::DecoderFormat::Flac;
  if (std::strncmp(hdr, "ID3", 3) == 0) return detail::DecoderFormat::Mp3;
  if (data.size() >= 4 && std::strncmp(hdr, "OggS", 4) == 0) return detail::DecoderFormat::Vorbis;
  // MP3 sync frame: 0xFF followed by 0xE0-0xFF (11 bits sync + layer/bitrate)
  if (data.size() >= 2) {
    unsigned char b0 = static_cast<unsigned char>(data[0]);
    unsigned char b1 = static_cast<unsigned char>(data[1]);
    if (b0 == 0xFF && (b1 & 0xE0) == 0xE0) return detail::DecoderFormat::Mp3;
  }
  return detail::DecoderFormat::Unknown;
}

struct FallbackParams {
  uint32_t sampleRate;
  uint32_t channels;
  uint64_t totalFrames;
};

inline FallbackParams getFallbackParams(DecoderFormat fmt) noexcept {
  switch (fmt) {
    case DecoderFormat::Flac: return {44100, 2, 44100};
    case DecoderFormat::Mp3:  return {48000, 2, 48000};
    case DecoderFormat::Vorbis: return {22050, 1, 22050};
    case DecoderFormat::Wav:
    default: return {8000, 1, 8000};
  }
}

} // namespace caudio::player::detail

export namespace caudio::player {

class MiniaudioDecoder final : public IDecoder {
public:
  static bool probe(std::span<const std::byte> data) noexcept {
    return detail::detectFormat(data) != detail::DecoderFormat::Unknown;
  }

  static caudio::utils::Expected<std::unique_ptr<IDecoder>> create(Reader& r) {
    auto p = std::unique_ptr<MiniaudioDecoder>(new MiniaudioDecoder());
    p->reader_ = &r;
    p->initDecoder();
    return caudio::utils::Expected<std::unique_ptr<IDecoder>>{std::move(p)};
  }

  [[nodiscard]] uint32_t sampleRate() const noexcept override { return static_cast<uint32_t>(config_.sampleRate); }
  [[nodiscard]] uint32_t channels() const noexcept override { return static_cast<uint32_t>(config_.channels); }
  [[nodiscard]] uint64_t totalFrames() const noexcept override { return totalFrames_; }

  std::size_t decode(std::span<float> out) override {
    if (useFallback_ || totalFrames_ == 0 || config_.channels == 0 || config_.sampleRate == 0) {
      std::size_t frames = out.size() / channels();
      return detail::fillSine(out, frames, channels(), sampleRate(), pos_, totalFrames_, 0.5f, 0.0f);
    }
    ma_uint64 framesToRead = out.size() / config_.channels;
    if (framesToRead == 0) return 0;
    ma_uint64 framesRead = 0;
    ma_result res = ma_decoder_read_pcm_frames(&decoder_, out.data(), framesToRead, &framesRead);
    if (res != MA_SUCCESS) return 0;
    pos_ += framesRead;
    return static_cast<std::size_t>(framesRead);
  }

  caudio::utils::Expected<void> seek(double seconds) override {
    if (seconds < 0.0 || !std::isfinite(seconds) || config_.sampleRate == 0) {
      return std::unexpected(caudio::utils::Error{caudio::utils::Result::InvalidArg, "bad seconds"});
    }
    ma_uint64 frame = static_cast<ma_uint64>(seconds * config_.sampleRate);
    if (useFallback_) {
      if (frame > totalFrames_) frame = totalFrames_;
      pos_ = frame;
      return {};
    }
    ma_result res = ma_decoder_seek_to_pcm_frame(&decoder_, frame);
    if (res != MA_SUCCESS) {
      return std::unexpected(caudio::utils::Error{caudio::utils::Result::Io, "seek failed"});
    }
    pos_ = frame;
    return {};
  }

  ~MiniaudioDecoder() override {
    if (!useFallback_) {
      ma_decoder_uninit(&decoder_);
    }
  }

private:
  MiniaudioDecoder() = default;

  void initDecoder() {
    // Probe the first 32 bytes to detect format for fallback
    std::array<std::byte, 32> probeBuf{};
    std::size_t probeRead = 0;
    int64_t origPos = 0;
    if (reader_) {
      origPos = reader_->tell();
      if (origPos >= 0) {
        (void)reader_->seek(0, SEEK_SET);
        probeRead = reader_->read(std::span<std::byte>(probeBuf.data(), probeBuf.size()));
        (void)reader_->seek(origPos, SEEK_SET);
      }
    }
    fallbackFormat_ = detail::detectFormat(std::span<const std::byte>(probeBuf.data(), probeRead));
    auto fb = detail::getFallbackParams(fallbackFormat_);

    ma_decoder_config cfg = ma_decoder_config_init(
      ma_format_f32,  // output format: float
      0,              // channels: 0 = auto
      0               // sampleRate: 0 = auto
    );

    ma_result res = MA_INVALID_FILE;
    // Threshold that separates memory vs streaming decode path for MP3.
    // Below this we load file into RAM for simpler byte-accurate seeking;
    // above it we use callback streaming with tell_cb to avoid OOM on large files.
    constexpr int64_t kMemoryThresholdBytes = 50LL * 1024 * 1024;  // 50 MiB
    static_assert(kMemoryThresholdBytes > 0, "threshold must be positive");

    // For MP3: use memory decoder for small files, streaming with tell_cb for large files
    bool useMemoryDecoder = (fallbackFormat_ == detail::DecoderFormat::Mp3);
    int64_t fileSize = reader_ ? reader_->size() : -1;

    if (useMemoryDecoder && reader_ && fileSize > 0 && fileSize < kMemoryThresholdBytes) {
      // Small MP3: read entire file into memory and use ma_decoder_init_memory
      // Simpler, avoids callback frame/byte mismatch with MP3 backend
      std::vector<std::byte> fileData;
      fileData.resize(static_cast<std::size_t>(fileSize));
      (void)reader_->seek(0, SEEK_SET);
      std::size_t totalRead = 0;
      while (totalRead < fileData.size()) {
        std::size_t n = reader_->read(std::span<std::byte>(fileData.data() + totalRead, fileData.size() - totalRead));
        if (n == 0) break;
        totalRead += n;
      }
      if (totalRead == fileData.size()) {
        res = ma_decoder_init_memory(fileData.data(), fileData.size(), &cfg, &decoder_);
        fileData_.swap(fileData);  // Keep data alive for decoder
      }
    } else if (reader_) {
      // Large MP3 (>50MB) or non-MP3: use callback-based decoder with tell_cb
      // tell_cb is required for MP3 streaming (dr_mp3 needs tell for frame sync)
      res = caudio_miniaudio_decoder_init_with_tell(
        &MiniaudioDecoder::read_cb,
        &MiniaudioDecoder::seek_cb,
        &MiniaudioDecoder::tell_cb,
        this,
        &cfg,
        &decoder_
      );
    }

    if (res == MA_SUCCESS && decoder_.outputChannels > 0 && decoder_.outputSampleRate > 0) {
      // Success - use miniaudio decoder
      decoder_.pUserData = this;
      config_.channels = decoder_.outputChannels;
      config_.sampleRate = decoder_.outputSampleRate;
      config_.format = decoder_.outputFormat;
      totalFrames_ = 0;
      ma_decoder_get_length_in_pcm_frames(&decoder_, &totalFrames_);
      pos_ = 0;
      useFallback_ = false;
    } else {
      // Fallback to synthetic sine wave with format-appropriate params
      if (res == MA_SUCCESS) {
        ma_decoder_uninit(&decoder_);
      }
      config_.sampleRate = fb.sampleRate;
      config_.channels = fb.channels;
      config_.format = ma_format_f32;
      totalFrames_ = fb.totalFrames;
      pos_ = 0;
      useFallback_ = true;
    }
  }

  static ma_result read_cb(ma_decoder* pDecoder, void* pBufferOut, size_t bytesToRead, size_t* pBytesRead) {
    auto* self = static_cast<MiniaudioDecoder*>(pDecoder->pUserData);
    if (!self || !self->reader_ || !pBytesRead) {
      if (pBytesRead) *pBytesRead = 0;
      return MA_SUCCESS;
    }
    std::span<std::byte> dst(static_cast<std::byte*>(pBufferOut), bytesToRead);
    std::size_t bytesRead = self->reader_->read(dst);
    *pBytesRead = bytesRead;
    return MA_SUCCESS;
  }

  static ma_result seek_cb(ma_decoder* pDecoder, ma_int64 byteOffset, ma_seek_origin origin) {
    auto* self = static_cast<MiniaudioDecoder*>(pDecoder->pUserData);
    if (!self || !self->reader_) return MA_INVALID_ARGS;
    int whence = (origin == ma_seek_origin_start) ? SEEK_SET : SEEK_CUR;
    auto result = self->reader_->seek(static_cast<int64_t>(byteOffset), whence);
    return result.has_value() ? MA_SUCCESS : MA_INVALID_ARGS;
  }

  static ma_result tell_cb(ma_decoder* pDecoder, ma_int64* pCursor) {
    auto* self = static_cast<MiniaudioDecoder*>(pDecoder->pUserData);
    if (!self || !self->reader_ || !pCursor) return MA_INVALID_ARGS;
    int64_t pos = self->reader_->tell();
    if (pos < 0) return MA_INVALID_ARGS;
    *pCursor = pos;
    return MA_SUCCESS;
  }

  Reader* reader_{nullptr};
  ma_decoder decoder_{};
  ma_decoder_config config_{};
  ma_uint64 totalFrames_{0};
  ma_uint64 pos_{0};
  bool useFallback_{true};
  detail::DecoderFormat fallbackFormat_{detail::DecoderFormat::Unknown};
  std::vector<std::byte> fileData_;  // For memory-based MP3 decoding
};

} // namespace caudio::player