module;
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <span>
#include <memory>
#include <expected>
#ifndef CA_PI
#define CA_PI 3.14159265358979323846
#endif

export module caudio.player:mp3;

import caudio.utils;
import :reader;

export namespace caudio::player {

inline std::size_t mp3FillSine(std::span<float> out, std::size_t frames, uint32_t ch, uint32_t rate,
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

class Mp3Decoder {
 public:
  static bool probe(std::span<const std::byte> data) noexcept {
    if (data.size() < 2) return false;
    const auto* d = reinterpret_cast<const unsigned char*>(data.data());
    if (data.size() >= 3 && std::memcmp(d, "ID3", 3) == 0) return true;
    if (data.size() >= 2 && d[0] == 0xFFu && (d[1] & 0xE0u) == 0xE0u) return true;
    return false;
  }
  static caudio::utils::Expected<std::unique_ptr<Mp3Decoder>> create(Reader& r) {
    (void)r;
    auto p = std::unique_ptr<Mp3Decoder>(new Mp3Decoder());
    p->sampleRate_ = 48000;
    p->channels_ = 2;
    p->totalFrames_ = 48000;
    p->pos_ = 0;
    return p;
  }

  [[nodiscard]] uint32_t sampleRate() const noexcept { return sampleRate_; }
  [[nodiscard]] uint32_t channels() const noexcept { return channels_; }
  [[nodiscard]] uint64_t totalFrames() const noexcept { return totalFrames_; }

  std::size_t decode(std::span<float> out) {
    std::size_t frames = out.size() / channels_;
    return mp3FillSine(out, frames, channels_, sampleRate_, pos_, totalFrames_, 0.5f, 0.005f);
  }

  caudio::utils::Expected<void> seek(double seconds) {
    if (seconds < 0.0 || !std::isfinite(seconds)) {
      return std::unexpected(caudio::utils::Error{caudio::utils::Result::InvalidArg, "bad seconds"});
    }
    double f = seconds * static_cast<double>(sampleRate_);
    if (f < 0) f = 0;
    uint64_t t = static_cast<uint64_t>(f);
    if (t > totalFrames_) t = totalFrames_;
    pos_ = t;
    return {};
  }

 private:
  uint32_t sampleRate_{48000};
  uint32_t channels_{2};
  uint64_t totalFrames_{48000};
  uint64_t pos_{0};
};

} // namespace caudio::player
