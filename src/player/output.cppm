module;
#include "miniaudio.h"
#include <cstdint>
#include <atomic>
#include <span>
#include <expected>
#include <memory>
#include <string>
#include <cmath>
#include <mutex>
#include <thread>

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
    stop();
  }

  static Expected create(const Config& cfg) {
    auto* out = new AudioOutput();
    if (!out->init(cfg)) {
      delete out;
      return std::unexpected(caudio::utils::Error{caudio::utils::Result::Device, "miniaudio device init failed"});
    }
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

  bool isPlaying() const noexcept {
    return running_.load(std::memory_order_acquire);
  }

  void start() {
    if (!running_.load(std::memory_order_acquire)) {
      running_.store(true, std::memory_order_release);
      ma_device_start(&device_);
    }
  }

  void stop() {
    if (running_.load(std::memory_order_acquire)) {
      running_.store(false, std::memory_order_release);
      ma_device_stop(&device_);
    }
    ma_device_uninit(&device_);
  }

  bool init(const Config& cfg) {
    cfg_ = cfg;

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = cfg.channels;
    deviceConfig.sampleRate = cfg.sampleRate;
    deviceConfig.dataCallback = &AudioOutput::dataCallback;
    deviceConfig.pUserData = this;

    ma_result res = ma_device_init(nullptr, &deviceConfig, &device_);
    if (res != MA_SUCCESS) {
      return false;
    }

    res = ma_device_start(&device_);
    if (res != MA_SUCCESS) {
      ma_device_uninit(&device_);
      return false;
    }

    // Debug: print device info
    printf("Audio device started: format=%d, channels=%d, sampleRate=%d\n",
           device_.playback.format, device_.playback.channels, device_.sampleRate);
    fflush(stdout);

    return true;
  }

  static void dataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    auto* self = static_cast<AudioOutput*>(pDevice->pUserData);
    if (!self) return;

    float* output = static_cast<float*>(pOutput);
    ma_uint32 channels = pDevice->playback.channels;
    ma_uint32 totalSamples = frameCount * channels;

    std::size_t generated = 0;
    if (self->cfg_.ring) {
      generated = self->cfg_.ring->read(std::span<float>(output, totalSamples));
    }

    // Fallback: generate 440Hz sine wave when ring buffer empty (for testing)
    if (generated < totalSamples) {
      static thread_local double phase = 0.0;
      const double freq = 440.0;
      const double sampleRate = static_cast<double>(pDevice->sampleRate);
      const double phaseInc = 2.0 * 3.141592653589793 * freq / sampleRate;

      for (std::size_t i = generated; i < totalSamples; ++i) {
        output[i] = static_cast<float>(std::sin(phase) * 0.3f);
        phase += phaseInc;
        if (phase >= 2.0 * 3.141592653589793) phase -= 2.0 * 3.141592653589793;
      }
    }

    // Apply volume
    float vol = self->volume_.load(std::memory_order_relaxed);
    if (vol != 1.0f) {
      for (ma_uint32 i = 0; i < totalSamples; ++i) {
        output[i] *= vol;
      }
    }
  }

  Config cfg_;
  ma_device device_{};
  std::atomic<float> volume_{1.0f};
  std::atomic<bool> running_{false};
};

} // namespace caudio::player