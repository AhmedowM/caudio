module;
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <mutex>
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
        auto* out = new AudioOutput();
        if (!out->init(cfg)) {
            delete out;
            return std::unexpected(caudio::utils::Error{caudio::utils::Result::Device,
                                                        "miniaudio device init failed"});
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
        for (auto& s : buf)
            s = 0.0f;
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

        // Do not auto-start: caller must call start() after preroll to avoid initial underrun
        running_.store(false, std::memory_order_release);
        initialized_.store(true, std::memory_order_release);

        // Debug: print device info (non-RT, init only)
        std::printf("Audio device ready: format=%d, channels=%d, sampleRate=%d\n",
                    device_.playback.format, device_.playback.channels, device_.sampleRate);
        std::fflush(stdout);

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

        std::size_t generatedFrames = 0;
        if (self->cfg_.ring) {
            generatedFrames = self->cfg_.ring->read(std::span<float>(output, totalSamples));
        }
        std::size_t generatedSamples = generatedFrames * channels;

        // Underrun: fill remainder with silence (no beep)
        if (generatedSamples < totalSamples) {
            for (std::size_t i = generatedSamples; i < totalSamples; ++i)
                output[i] = 0.0f;
        }

        // Apply volume
        float vol = self->volume_.load(std::memory_order_relaxed);
        if (vol != 1.0f) {
            for (ma_uint32 i = 0; i < totalSamples; ++i) {
                output[i] *= vol;
            }
        }

        // Note: generated is in frames, totalSamples = frameCount * channels
        // No zero-fill needed here since miniaudio passes pre-zeroed buffer
    }

    Config cfg_;
    ma_device device_{};
    std::atomic<float> volume_{1.0f};
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    double phase_{0.0};
};

} // namespace caudio::player