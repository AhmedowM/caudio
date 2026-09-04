module;
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <span>
#include <memory>
#include <expected>

export module caudio.player:mp3;

import caudio.utils;
import :reader;
import :decoder_interface;
import :decoder_common;

export namespace caudio::player {

class Mp3Decoder final : public IDecoder {
public:
  static bool probe(std::span<const std::byte> data) noexcept {
    if (data.size() < 2) return false;
    const auto* d = reinterpret_cast<const unsigned char*>(data.data());
    if (data.size() >= 3 && std::memcmp(d, "ID3", 3) == 0) return true;
    if (data.size() >= 2 && d[0] == 0xFFu && (d[1] & 0xE0u) == 0xE0u) return true;
    return false;
  }

  static caudio::utils::Expected<std::unique_ptr<IDecoder>> create(Reader& r) {
    (void)r;
    auto p = std::unique_ptr<Mp3Decoder>(new Mp3Decoder());
    p->sampleRate_ = 48000;
    p->channels_ = 2;
    p->totalFrames_ = 48000;
    p->pos_ = 0;
    return caudio::utils::Expected<std::unique_ptr<IDecoder>>{std::unique_ptr<IDecoder>(std::move(p))};
  }

  [[nodiscard]] uint32_t sampleRate() const noexcept override { return sampleRate_; }
  [[nodiscard]] uint32_t channels() const noexcept override { return channels_; }
  [[nodiscard]] uint64_t totalFrames() const noexcept override { return totalFrames_; }

  std::size_t decode(std::span<float> out) override {
    std::size_t frames = out.size() / channels_;
    return detail::fillSine(out, frames, channels_, sampleRate_, pos_, totalFrames_, 0.5f, 0.005f);
  }

  caudio::utils::Expected<void> seek(double seconds) override {
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
  Mp3Decoder() = default;
  uint32_t sampleRate_{48000};
  uint32_t channels_{2};
  uint64_t totalFrames_{48000};
  uint64_t pos_{0};
};

} // namespace caudio::player