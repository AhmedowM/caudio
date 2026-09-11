module;
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <mutex>
#include <ranges>
#include <span>
#include <string>
#include <thread>

#include "miniaudio.h"

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
        shutdown();
    }

    static Expected create(const Config& cfg) {
        auto out = std::make_unique<AudioOutput>();
        if (!out->init(cfg)) {
            return std::unexpected(caudio::utils::Error{caudio::utils::Result::Device,
                                                        "miniaudio device init failed"});
        }
        return out;
    }

    static inline float clampVolume(float v) noexcept {
        if (!std::isfinite(v)) return 0.0f;
        return std::clamp(v, 0.0f, 1.0f);
    }

    void setVolume(float vol) {
        volume_.store(clampVolume(vol), std::memory_order_relaxed);
    }

    float volume() const noexcept {
        return volume_.load(std::memory_order_relaxed);
    }

    void testFill(std::span<float> buf) const noexcept {
        std::ranges::fill(buf, 0.0f);
    }

    // Shared lock-free helper: ring read + zero-fill + volume. No allocation, no format.
    static void fillFromRing(std::span<float> out, caudio::utils::SpscRing<float>* ring,
                             uint32_t channels, float vol) noexcept {
        if (out.empty())
            return;
        std::size_t totalSamples = out.size();
        std::size_t generatedFrames = 0;
        if (ring) {
            generatedFrames = ring->read(std::span<float>(out.data(), totalSamples));
        }
        std::size_t generatedSamples = generatedFrames * channels;
        if (generatedSamples < totalSamples) {
            std::ranges::fill(out.subspan(generatedSamples), 0.0f);
        }
        if (vol != 1.0f) {
            for (std::size_t i = 0; i < totalSamples; ++i)
                out[i] *= vol;
        }
    }

    // Test-accessible wrapper that mimics dataCallback logic without needing ma_device.
    // Reads from ring (if set), applies volume, zero-fills remainder. Used for deterministic tests.
    void fillForTest(std::span<float> out) noexcept {
        uint32_t channels = cfg_.channels ? cfg_.channels : 1;
        float vol = volume_.load(std::memory_order_relaxed);
        fillFromRing(out, cfg_.ring, channels, vol);
    }

    bool isPlaying() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    void start() {
        if (!initialized_.load(std::memory_order_acquire))
            return;
        bool expected = false;
        if (running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            ma_device_start(&device_);
        }
    }

    void stop() {
        bool expected = true;
        if (running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
            ma_device_stop(&device_);
        }
    }

    void shutdown() noexcept {
        stop();
        if (initialized_.exchange(false, std::memory_order_acq_rel)) {
            ma_device_uninit(&device_);
        }
    }

    bool init(const Config& cfg) {
        if (cfg.channels == 0 || cfg.channels > 32 || cfg.sampleRate == 0)
            return false;
        cfg_ = cfg;
        float v = clampVolume(cfg.volume);
        volume_.store(v, std::memory_order_relaxed);

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
        initialized_.store(true, std::memory_order_release);

        // @pre caller must start() after preroll, cap/2 frames
        running_.store(false, std::memory_order_release);

        return true;
    }

    static void dataCallback(ma_device* pDevice, void* pOutput, const void* pInput,
                             ma_uint32 frameCount) {
        (void)pInput;
        auto* self = static_cast<AudioOutput*>(pDevice->pUserData);
        if (!self)
            return;

        float* output = static_cast<float*>(pOutput);
        ma_uint32 channels = pDevice->playback.channels;
        ma_uint32 totalSamples = frameCount * channels;
        float vol = self->volume_.load(std::memory_order_relaxed);
        // lock-free, no allocation
        fillFromRing(std::span<float>(output, totalSamples), self->cfg_.ring, channels, vol);
    }

  private:
    Config cfg_;
    ma_device device_{};
    std::atomic<float> volume_{1.0f};
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
};

} // namespace caudio::player