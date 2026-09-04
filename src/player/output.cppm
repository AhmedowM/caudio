module;
#include "miniaudio.h"
#include <cstdint>
#include <atomic>
#include <span>
#include <expected>
#include <memory>
#include <string>

export module caudio.player:output;

import caudio.utils;

export namespace caudio::player {

class AudioOutput {
public:
  using Expected = std::expected<std::unique_ptr<AudioOutput>, caudio::utils::Error>;

  struct Config {
    uint32_t sampleRate = 48000;
    uint32_t channels = 2;
    caudio::utils::SpscRing<float>* ring = nullptr;
    float volume = 1.0f;
  };

  AudioOutput() = default;

  ~AudioOutput() {
    // Stub: device cleanup handled externally
    (void)this;
  }

  static Expected create(const Config& cfg) {
    auto* out = new AudioOutput();
    (void)cfg; // TODO: Full miniaudio device init
    return std::unique_ptr<AudioOutput>(out);
  }

  void setVolume(float vol) {
    volume_.store(vol < 0.0f ? 0.0f : (vol > 1.0f ? 1.0f : vol), std::memory_order_relaxed);
  }

  float volume() const noexcept {
    return volume_.load(std::memory_order_relaxed);
  }

  void testFill(std::span<float> buf) const noexcept {
    for (auto& s : buf) s = 0.0f;
  }

private:
  caudio::utils::SpscRing<float>* ring_{nullptr};
  std::atomic<float> volume_{1.0f};
};

} // namespace caudio::player