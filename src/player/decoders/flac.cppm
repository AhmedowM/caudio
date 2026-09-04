module;
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <span>
#include <memory>
#include <expected>

export module caudio.player:flac;

import caudio.utils;
import :reader;
import :decoder_interface;
import :decoder_common;

export namespace caudio::player {

class FlacDecoder final : public IDecoder {
public:
  static bool probe(std::span<const std::byte> data) noexcept {
    if (data.size() < 4) return false;
    return std::memcmp(data.data(), "fLaC", 4) == 0;
  }

  static caudio::utils::Expected<std::unique_ptr<IDecoder>> create(Reader& r) {
    (void)r;
    auto p = std::unique_ptr<FlacDecoder>(new FlacDecoder());
    p->sampleRate_ = 44100;
    p->channels_ = 2;
    p->totalFrames_ = 44100;
    p->pos_ = 0;
    return caudio::utils::Expected<std::unique_ptr<IDecoder>>{std::unique_ptr<IDecoder>(std::move(p))};
  }

  [[nodiscard]] uint32_t sampleRate() const noexcept override { return sampleRate_; }
  [[nodiscard]] uint32_t channels() const noexcept override { return channels_; }
  [[nodiscard]] uint64_t totalFrames() const noexcept override { return totalFrames_; }

  std::size_t decode(std::span<float> out) override {
    std::size_t frames = out.size() / channels_;
    return detail::fillSine(out, frames, channels_, sampleRate_, pos_, totalFrames_, 0.5f, 0.01f);
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
  FlacDecoder() = default;
  uint32_t sampleRate_{44100};
  uint32_t channels_{2};
  uint64_t totalFrames_{44100};
  uint64_t pos_{0};
};

} // namespace caudio::player