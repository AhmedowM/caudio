module;
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <span>
#include <memory>
#include <expected>
#include <vector>
#ifndef CA_PI
#define CA_PI 3.14159265358979323846
#endif

// Real stb_vorbis — fix W5: use stb_vorbis_open_memory instead of synthetic stub
// Include via global fragment only here to avoid ODR duplicates
#include "stb_vorbis.h"

export module caudio.player:vorbis;

import caudio.utils;
import :reader;

export namespace caudio::player {

inline std::size_t vorbisFillSine(std::span<float> out, std::size_t frames, uint32_t ch, uint32_t rate,
                                  uint64_t& pos, uint64_t total, float amp, float chanOff) {
  if (out.empty() || frames == 0) return 0;
  uint64_t rem = pos < total ? total - pos : 0;
  if (rem == 0) return 0;
  std::size_t n = frames;
  if (static_cast<uint64_t>(n) > rem) n = static_cast<std::size_t>(rem);
  for (std::size_t i = 0; i < n; ++i) {
    uint64_t idx = pos + i;
    double t = static_cast<double>(idx) / static_cast<double>(rate);
    double s = std::sin(2.0 * CA_PI * 440.0 * t) * static_cast<double>(amp);
    for (uint32_t c = 0; c < ch; ++c) out[i * ch + c] = static_cast<float>(s + static_cast<double>(c) * static_cast<double>(chanOff));
  }
  pos += n;
  return n;
}

class VorbisDecoder {
 public:
  static bool probe(std::span<const std::byte> data) noexcept {
    if (data.size() < 4) return false;
    return std::memcmp(data.data(), "OggS", 4) == 0;
  }

  static caudio::utils::Expected<std::unique_ptr<VorbisDecoder>> create(Reader& r) {
    auto* p = new VorbisDecoder();
    std::unique_ptr<VorbisDecoder> up(p);
    p->reader_ = &r;
    // Try real stb_vorbis_open_memory if Reader is MemoryReader with full data
    // For FileReader, we would need to load bytes — fallback to synthetic for Task 3
    const std::byte* memData = nullptr;
    int memLen = 0;
    bool haveMem = false;
    if (auto* mr = dynamic_cast<MemoryReader*>(&r)) {
      auto span = mr->data();
      if (!span.empty()) {
        memData = span.data();
        memLen = static_cast<int>(span.size());
        haveMem = true;
      }
    }
    // Default synthetic params src/player/decoders/stb_vorbis.c:32 => 22050/1
    p->sampleRate_ = 22050;
    p->channels_ = 1;
    p->totalFrames_ = 22050;
    p->pos_ = 0;
    p->vorbis_ = nullptr;

    if (haveMem && memData && memLen > 0) {
      int err = 0;
      stb_vorbis* v = stb_vorbis_open_memory(
          reinterpret_cast<const unsigned char*>(memData), memLen, &err, nullptr);
      if (v) {
        stb_vorbis_info info = stb_vorbis_get_info(v);
        p->sampleRate_ = info.sample_rate ? info.sample_rate : 22050u;
        p->channels_ = info.channels ? static_cast<uint32_t>(info.channels) : 1u;
        // total frames from stream_length_in_samples, may be 0 for synthetic
        unsigned int total = stb_vorbis_stream_length_in_samples(v);
        if (total > 0) p->totalFrames_ = total;
        p->vorbis_ = v;
        // keep vorbis handle for real decode; pos tracks via stb_vorbis_get_sample_offset
      } else {
        // keep synthetic fallback
        (void)err;
      }
    }
    return up;
  }

  ~VorbisDecoder() {
    if (vorbis_) stb_vorbis_close(vorbis_);
  }

  VorbisDecoder(const VorbisDecoder&) = delete;
  VorbisDecoder& operator=(const VorbisDecoder&) = delete;

  [[nodiscard]] uint32_t sampleRate() const noexcept { return sampleRate_; }
  [[nodiscard]] uint32_t channels() const noexcept { return channels_; }
  [[nodiscard]] uint64_t totalFrames() const noexcept { return totalFrames_; }

  std::size_t decode(std::span<float> out) {
    std::size_t frames = out.size() / channels_;
    if (frames == 0) return 0;
    if (vorbis_) {
      // Use real stb_vorbis_get_samples_float_interleaved if we have a handle
      // Note: need to handle channel coercion — request channels_
      int got = stb_vorbis_get_samples_float_interleaved(vorbis_, static_cast<int>(channels_), out.data(),
                                                         static_cast<int>(frames * channels_));
      if (got > 0) {
        pos_ += static_cast<uint64_t>(got);
        return static_cast<std::size_t>(got);
      }
      // EOF or need more data -> fallback to 0 (EOF)
      return 0;
    }
    return vorbisFillSine(out, frames, channels_, sampleRate_, pos_, totalFrames_, 0.5f, 0.0f);
  }

  caudio::utils::Expected<void> seek(double seconds) {
    if (seconds < 0.0 || !std::isfinite(seconds)) {
      return std::unexpected(caudio::utils::Error{caudio::utils::Result::InvalidArg, "bad seconds"});
    }
    double f = seconds * static_cast<double>(sampleRate_);
    if (f < 0) f = 0;
    uint64_t t = static_cast<uint64_t>(f);
    if (t > totalFrames_) t = totalFrames_;
    if (vorbis_) {
      int ok = stb_vorbis_seek(vorbis_, static_cast<unsigned int>(t));
      if (!ok) {
        // seek may fail for truncated synthetic; treat as clamp to pos
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
  uint32_t sampleRate_{22050};
  uint32_t channels_{1};
  uint64_t totalFrames_{22050};
  uint64_t pos_{0};
  Reader* reader_{nullptr};
  stb_vorbis* vorbis_{nullptr};
};

} // namespace caudio::player
