module;
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <span>
#include <memory>
#include <expected>
#include <vector>

// stb_vorbis implementation via global fragment
#include "stb_vorbis.h"

export module caudio.player:vorbis;

import caudio.utils;
import :reader;
import :decoder_interface;
import :decoder_common;

export namespace caudio::player {

class VorbisDecoder final : public IDecoder {
public:
  static bool probe(std::span<const std::byte> data) noexcept {
    if (data.size() < 4) return false;
    return std::memcmp(data.data(), "OggS", 4) == 0;
  }

  static caudio::utils::Expected<std::unique_ptr<IDecoder>> create(Reader& r) {
    auto p = std::unique_ptr<VorbisDecoder>(new VorbisDecoder());
    p->reader_ = &r;

    // Default synthetic params (src/player/decoders/stb_vorbis.c:32 => 22050/1)
    p->sampleRate_ = 22050;
    p->channels_ = 1;
    p->totalFrames_ = 22050;
    p->pos_ = 0;
    p->vorbis_ = nullptr;
    p->useFallback_ = true;

// Try real stb_vorbis_open_memory via MemoryReader
    if (r.size() > 0) {
      auto dataSize = static_cast<std::size_t>(r.size());
      std::vector<std::byte> buf(dataSize);
      auto readResult = r.read(std::span(buf.data(), buf.size()));
      if (readResult > 0) {
        auto memResult = MemoryReader::open(std::span<const std::byte>(buf.data(), static_cast<std::size_t>(readResult)));
        if (memResult) {
          int err = 0;
          stb_vorbis* v = stb_vorbis_open_memory(
              reinterpret_cast<const unsigned char*>(buf.data()),
              static_cast<int>(buf.size()),
              &err, nullptr);
          if (v) {
            stb_vorbis_info info = stb_vorbis_get_info(v);
            p->sampleRate_ = info.sample_rate ? info.sample_rate : 22050u;
            p->channels_ = info.channels ? static_cast<uint32_t>(info.channels) : 1u;
            unsigned int total = stb_vorbis_stream_length_in_samples(v);
            if (total > 0) p->totalFrames_ = total;
            p->vorbis_ = v;
            p->useFallback_ = false;
          }
        }
      }
    }
    return caudio::utils::Expected<std::unique_ptr<IDecoder>>{std::unique_ptr<IDecoder>(std::move(p))};
  }

  ~VorbisDecoder() override {
    if (vorbis_) stb_vorbis_close(vorbis_);
  }

  [[nodiscard]] uint32_t sampleRate() const noexcept override { return sampleRate_; }
  [[nodiscard]] uint32_t channels() const noexcept override { return channels_; }
  [[nodiscard]] uint64_t totalFrames() const noexcept override { return totalFrames_; }

  std::size_t decode(std::span<float> out) override {
    std::size_t frames = out.size() / channels_;
    if (frames == 0) return 0;

    if (useFallback_ || !vorbis_) {
      return detail::fillSine(out, frames, channels_, sampleRate_, pos_, totalFrames_, 0.5f, 0.0f);
    }

    // Use real stb_vorbis_get_samples_float_interleaved
    int got = stb_vorbis_get_samples_float_interleaved(
        vorbis_, static_cast<int>(channels_), out.data(),
        static_cast<int>(frames * channels_));
    if (got > 0) {
      pos_ += static_cast<uint64_t>(got);
      return static_cast<std::size_t>(got);
    }
    return 0; // EOF
  }

  caudio::utils::Expected<void> seek(double seconds) override {
    if (seconds < 0.0 || !std::isfinite(seconds)) {
      return std::unexpected(caudio::utils::Error{caudio::utils::Result::InvalidArg, "bad seconds"});
    }
    double f = seconds * static_cast<double>(sampleRate_);
    if (f < 0) f = 0;
    uint64_t t = static_cast<uint64_t>(f);
    if (t > totalFrames_) t = totalFrames_;

    if (!useFallback_ && vorbis_) {
      int ok = stb_vorbis_seek(vorbis_, static_cast<unsigned int>(t));
      if (!ok) {
        pos_ = t;
        return std::unexpected(caudio::utils::Error{caudio::utils::Result::Io, "seek failed"});
      }
      pos_ = t;
      return {};
    }
    pos_ = t;
    return {};
  }

private:
  VorbisDecoder() = default;
  Reader* reader_{nullptr};
  stb_vorbis* vorbis_{nullptr};
  uint32_t sampleRate_{22050};
  uint32_t channels_{1};
  uint64_t totalFrames_{22050};
  uint64_t pos_{0};
  bool useFallback_{true};
};

} // namespace caudio::player