module;
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <span>
#include <memory>
#include <expected>
#include <vector>

// dr_wav implementation via global fragment
#define DR_WAV_IMPLEMENTATION
#define DRWAV_NO_STDIO
#include "dr_wav.h"

export module caudio.player:wav;

import caudio.utils;
import :reader;
import :decoder_interface;
import :decoder_common;

export namespace caudio::player {

// WavDecoder using real drwav_init_ex with Reader callbacks + sine fallback
class WavDecoder final : public IDecoder {
public:
  static bool probe(std::span<const std::byte> data) noexcept {
    if (data.size() < 4) return false;
    return std::memcmp(data.data(), "RIFF", 4) == 0;
  }

  static caudio::utils::Expected<std::unique_ptr<IDecoder>> create(Reader& r) {
    auto p = std::unique_ptr<WavDecoder>(new WavDecoder());
    p->reader_ = &r;

    // Try real drwav_init_ex with Reader callbacks
    if (p->initDrWav()) {
      return caudio::utils::Expected<std::unique_ptr<IDecoder>>{std::unique_ptr<IDecoder>(std::move(p))};
    }

    // Fallback to synthetic sine 8000/1 like dr_wav.c:83
    p->sampleRate_ = 8000;
    p->channels_ = 1;
    p->totalFrames_ = 8000;
    p->pos_ = 0;
    p->useFallback_ = true;
    return caudio::utils::Expected<std::unique_ptr<IDecoder>>{std::unique_ptr<IDecoder>(std::move(p))};
  }

  [[nodiscard]] uint32_t sampleRate() const noexcept override { return sampleRate_; }
  [[nodiscard]] uint32_t channels() const noexcept override { return channels_; }
  [[nodiscard]] uint64_t totalFrames() const noexcept override { return totalFrames_; }

  std::size_t decode(std::span<float> out) override {
    if (useFallback_ || !wav_) {
      std::size_t frames = out.size() / channels_;
      return detail::fillSine(out, frames, channels_, sampleRate_, pos_, totalFrames_, 0.5f, 0.0f);
    }

    std::size_t frames = out.size() / channels_;
    if (frames == 0) return 0;

    // Use drwav_read_pcm_frames_f32 for float output
    drwav_uint64 framesRead = drwav_read_pcm_frames_f32(wav_.get(), frames, out.data());
    pos_ += static_cast<uint64_t>(framesRead);
    return static_cast<std::size_t>(framesRead);
  }

  caudio::utils::Expected<void> seek(double seconds) override {
    if (seconds < 0.0 || !std::isfinite(seconds)) {
      return std::unexpected(caudio::utils::Error{caudio::utils::Result::InvalidArg, "bad seconds"});
    }
    double f = seconds * static_cast<double>(sampleRate_);
    if (f < 0) f = 0;
    uint64_t t = static_cast<uint64_t>(f);
    if (t > totalFrames_) t = totalFrames_;

    if (useFallback_ || !wav_) {
      pos_ = t;
      return {};
    }

    if (!drwav_seek_to_pcm_frame(wav_.get(), t)) {
      pos_ = t;
      return std::unexpected(caudio::utils::Error{caudio::utils::Result::Io, "seek failed"});
    }
    pos_ = t;
    return {};
  }

  ~WavDecoder() override {
    if (wav_) {
      drwav_uninit(wav_.get());
    }
  }

private:
  WavDecoder() : wav_(new drwav{}, [](drwav* p) { if (p) drwav_uninit(p); }) {}

  bool initDrWav() {
    wav_ = std::unique_ptr<drwav, void(*)(drwav*)>(
      new drwav{},
      [](drwav* p) { if (p) drwav_uninit(p); }
    );
    if (!wav_) return false;

    drwav_bool32 ok = drwav_init_ex(
      wav_.get(),
      &WavDecoder::onRead,
      &WavDecoder::onSeek,
      &WavDecoder::onTell,
      nullptr, // onChunk
      this,    // pReadSeekTellUserData
      nullptr, // pChunkUserData
      0,       // flags
      nullptr  // allocationCallbacks
    );

    if (!ok) {
      wav_.reset();
      return false;
    }

    sampleRate_ = wav_->sampleRate;
    channels_ = wav_->channels;
    totalFrames_ = wav_->totalPCMFrameCount;
    pos_ = 0;
    useFallback_ = false;
    return true;
  }

  static size_t onRead(void* pUserData, void* pBufferOut, size_t bytesToRead) {
    auto* self = static_cast<WavDecoder*>(pUserData);
    if (!self->reader_) return 0;
    std::span<std::byte> dst(static_cast<std::byte*>(pBufferOut), bytesToRead);
    return self->reader_->read(dst);
  }

  static drwav_bool32 onSeek(void* pUserData, int offset, drwav_seek_origin origin) {
    auto* self = static_cast<WavDecoder*>(pUserData);
    if (!self->reader_) return DRWAV_FALSE;
    int whence = (origin == DRWAV_SEEK_SET) ? SEEK_SET : SEEK_CUR;
    auto result = self->reader_->seek(offset, whence);
    return result.has_value() ? DRWAV_TRUE : DRWAV_FALSE;
  }

  static drwav_bool32 onTell(void* pUserData, drwav_int64* pCursor) {
    auto* self = static_cast<WavDecoder*>(pUserData);
    if (!self->reader_) return DRWAV_FALSE;
    int64_t pos = self->reader_->tell();
    if (pos < 0) return DRWAV_FALSE;
    *pCursor = static_cast<drwav_int64>(pos);
    return DRWAV_TRUE;
  }

  Reader* reader_{nullptr};
  std::unique_ptr<drwav, void(*)(drwav*)> wav_;
  uint32_t sampleRate_{8000};
  uint32_t channels_{1};
  uint64_t totalFrames_{8000};
  uint64_t pos_{0};
  bool useFallback_{true};
};

} // namespace caudio::player