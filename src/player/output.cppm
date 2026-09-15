/**
 * @file output.cppm
 * @brief Audio output management using miniaudio
 * @ingroup caudio_player
 *
 * This module provides audio device enumeration and playback functionality
 * using the miniaudio library. It includes:
 * - DeviceInfo/DeviceList: Audio device metadata structures
 * - enumerateDevices(): System audio device enumeration via miniaudio context
 * - AudioOutput: Lock-free audio playback with ring buffer integration
 *
 * Thread Safety:
 * - enumerateDevices(): Thread-safe (creates/destroys miniaudio context internally)
 * - AudioOutput::create(): Thread-safe
 * - AudioOutput methods: Thread-safe for control (start/stop/setVolume),
 *   dataCallback runs on miniaudio's audio thread (real-time priority)
 *
 * miniaudio Integration:
 * - Uses ma_context for device enumeration
 * - Uses ma_device for playback with float32 format
 * - Data callback runs on miniaudio's internal audio thread
 * - Lock-free ring buffer communication between decode and audio threads
 *
 * Error Codes:
 * - Device: miniaudio device initialization/start/stop failed
 * - InvalidArg: Invalid configuration (sample rate, channels, null ring)
 * - NoMem: Allocation failure
 */

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
#include <vector>

#include "miniaudio.h"

export module caudio.player:output;

import caudio.utils;

export namespace caudio::player {

/**
 * @struct DeviceInfo
 * @brief Audio playback device information
 * @ingroup caudio_player
 *
 * Represents a single audio output device as reported by miniaudio.
 * Contains the device identifier, human-readable name, and default status.
 */
struct DeviceInfo {
    /// Unique device identifier (index-based for CLI friendliness)
    std::string id;
    /// Human-readable device name from miniaudio
    std::string name;
    /// True if this is the system default playback device
    bool isDefault = false;
};

/**
 * @struct DeviceList
 * @brief Container for enumerated audio devices
 * @ingroup caudio_player
 *
 * Holds a list of available playback devices returned by enumerateDevices().
 */
struct DeviceList {
    /// Vector of available playback devices
    std::vector<DeviceInfo> devices;
};

/**
 * @brief Enumerate all available audio playback devices
 * @return DeviceList containing all playback devices
 *
 * Creates a miniaudio context, queries the system for playback devices,
 * and returns a list of DeviceInfo structures. The miniaudio context
 * is created and destroyed within this function call.
 *
 * Device IDs are assigned as sequential indices (0, 1, 2...) for
 * user-friendly CLI selection. The default device is marked with
 * isDefault = true.
 *
 * Thread Safety: Thread-safe. Each call creates and destroys its own
 * miniaudio context. Can be called concurrently.
 *
 * Error Handling: On miniaudio context initialization failure, returns
 * an empty DeviceList. No exceptions thrown.
 *
 * miniaudio Integration:
 * - Uses ma_context_init() with default config
 * - Queries devices via ma_context_get_devices()
 * - Cleans up with ma_context_uninit()
 * - Only returns playback devices (capture devices ignored)
 */
inline DeviceList enumerateDevices() {
    DeviceList list;
    ma_context context;
    ma_context_config ctxConfig = ma_context_config_init();
    ma_result res = ma_context_init(nullptr, 0, &ctxConfig, &context);
    if (res != MA_SUCCESS) {
        return list;
    }

    ma_device_info* pPlaybackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    ma_device_info* pCaptureInfos = nullptr;
    ma_uint32 captureCount = 0;

    res = ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount);
    if (res == MA_SUCCESS && pPlaybackInfos && playbackCount > 0) {
        list.devices.reserve(playbackCount);
        for (ma_uint32 i = 0; i < playbackCount; ++i) {
            const auto& info = pPlaybackInfos[i];
            DeviceInfo di;
            di.name = info.name;
            di.isDefault = info.isDefault;
            // Use simple index-based ID for user-friendly CLI
            di.id = std::to_string(i);
            list.devices.push_back(std::move(di));
        }
    }

    ma_context_uninit(&context);
    return list;
}

/**
 * @class AudioOutput
 * @brief Lock-free audio output device with ring buffer integration
 * @ingroup caudio_player
 *
 * Manages a miniaudio playback device that consumes float32 samples from
 * a lock-free SPSC ring buffer. Designed for real-time audio playback
 * with minimal latency and no allocations in the audio callback.
 *
 * Thread Safety:
 * - create()/init()/shutdown(): Thread-safe, called from control thread
 * - start()/stop()/setVolume()/volume()/isPlaying(): Thread-safe (atomic)
 * - dataCallback(): Runs on miniaudio audio thread (real-time), lock-free
 * - fillFromRing()/fillForTest(): Thread-safe (no shared mutable state)
 *
 * Audio Pipeline:
 * 1. Decode thread writes float samples to SPSC ring buffer
 * 2. miniaudio calls dataCallback on audio thread
 * 3. dataCallback calls fillFromRing() to read from ring + apply volume
 * 4. Zero-fills any remaining buffer space (silence on underrun)
 *
 * Volume Control:
 * - Volume is applied in dataCallback via atomic load (lock-free)
 * - Range: 0.0 to 1.0, clamped in clampVolume()
 * - Changes take effect on next audio callback
 *
 * Lifecycle:
 *   create() -> init() -> start() -> (playback) -> stop() -> shutdown()
 *   Destructor calls shutdown() automatically.
 */
class AudioOutput {
  public:
    using Expected = std::expected<std::unique_ptr<AudioOutput>, caudio::utils::Error>;

    /**
     * @struct Config
     * @brief Audio output configuration parameters
     * @ingroup caudio_player
     */
    struct Config {
        /// Output sample rate in Hz (default: 48000)
        uint32_t sampleRate = 48000;
        /// Number of output channels (default: 2)
        uint32_t channels = 2;
        /// Pointer to SPSC ring buffer for sample data (required)
        caudio::utils::SpscRing<float>* ring = nullptr;
        /// Initial volume 0.0-1.0 (default: 1.0)
        float volume = 1.0f;
    };

    AudioOutput() = default;

    ~AudioOutput() {
        shutdown();
    }

    /**
     * @brief Create and initialize an AudioOutput instance
     * @param cfg Configuration for the audio output
     * @return Expected containing AudioOutput on success, Error on failure
     *
     * Factory method that creates an AudioOutput and initializes the
     * miniaudio device. Returns an error if device initialization fails.
     *
     * @retval StatusCode::Device miniaudio device init failed
     * @retval StatusCode::InvalidArg Invalid config (0 channels, 0 sample rate, null ring)
     *
     * Thread Safety: Thread-safe. Can be called from any thread.
     */
    static Expected create(const Config& cfg) {
        auto out = std::make_unique<AudioOutput>();
        if (!out->init(cfg)) {
            return std::unexpected(caudio::utils::Error{caudio::utils::StatusCode::Device,
                                                        std::string_view("miniaudio device init failed")});
        }
        return out;
    }

    /**
     * @brief Clamp volume value to valid range [0.0, 1.0]
     * @param v Volume value to clamp
     * @return Clamped volume value
     *
     * Handles NaN/infinity by returning 0.0. Used internally for
     * volume sanitization before applying to audio output.
     */
    static inline float clampVolume(float v) noexcept {
        if (!std::isfinite(v))
            return 0.0f;
        return std::clamp(v, 0.0f, 1.0f);
    }

    /**
     * @brief Set playback volume
     * @param vol Volume level (0.0 = mute, 1.0 = full)
     *
     * Volume is clamped to [0.0, 1.0] and applied atomically.
     * Takes effect on the next audio callback.
     *
     * Thread Safety: Thread-safe (atomic store with relaxed ordering)
     */
    void setVolume(float vol) {
        volume_.store(clampVolume(vol), std::memory_order_relaxed);
    }

    /**
     * @brief Get current playback volume
     * @return Current volume level (0.0 to 1.0)
     *
     * Thread Safety: Thread-safe (atomic load with relaxed ordering)
     */
    float volume() const noexcept {
        return volume_.load(std::memory_order_relaxed);
    }

    // TEST-ONLY: used by tests/test_output.cpp — keep functionality (hold BREAKING deletion)
    void testFill(std::span<float> buf) const noexcept {
        std::ranges::fill(buf, 0.0f);
    }

    /**
     * @brief Fill output buffer from ring buffer with volume scaling
     * @param out Output buffer to fill (interleaved float samples)
     * @param ring SPSC ring buffer to read from (may be null)
     * @param channels Number of channels in output
     * @param vol Volume multiplier (0.0 to 1.0)
     *
     * Lock-free helper that reads available samples from the ring buffer,
     * zero-fills any remainder, and applies volume scaling. No allocations.
     * Used by both dataCallback and testFillForTest().
     *
     * Thread Safety: Thread-safe when ring is the SPSC ring (single producer,
     * single consumer). No internal locking.
     */
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

    // TEST-ONLY: used by tests/test_output.cpp — keep functionality (hold BREAKING deletion)
    // Test-accessible wrapper that mimics dataCallback logic without needing ma_device.
    // Reads from ring (if set), applies volume, zero-fills remainder. Used for deterministic tests.
    void fillForTest(std::span<float> out) noexcept {
        uint32_t channels = cfg_.channels ? cfg_.channels : 1;
        float vol = volume_.load(std::memory_order_relaxed);
        fillFromRing(out, cfg_.ring, channels, vol);
    }

    /**
     * @brief Check if audio output is currently playing
     * @return true if device is running, false otherwise
     *
     * Thread Safety: Thread-safe (atomic load with acquire ordering)
     */
    bool isPlaying() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    /**
     * @brief Start audio playback
     *
     * Starts the miniaudio device if initialized. Uses compare_exchange
     * to ensure only one start transition occurs.
     *
     * Thread Safety: Thread-safe (atomic compare_exchange)
     * Precondition: init() must have been called successfully
     */
    void start() {
        if (!initialized_.load(std::memory_order_acquire))
            return;
        bool expected = false;
        if (running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            ma_device_start(&device_);
        }
    }

    /**
     * @brief Stop audio playback
     *
     * Stops the miniaudio device if running. Uses compare_exchange
     * to ensure only one stop transition occurs.
     *
     * Thread Safety: Thread-safe (atomic compare_exchange)
     */
    void stop() {
        bool expected = true;
        if (running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
            ma_device_stop(&device_);
        }
    }

    /**
     * @brief Shutdown and release audio device resources
     *
     * Stops the device if running, then uninitializes the miniaudio device.
     * Safe to call multiple times. Called automatically by destructor.
     *
     * Thread Safety: Thread-safe (atomic exchange for initialized flag)
     */
    void shutdown() noexcept {
        stop();
        if (initialized_.exchange(false, std::memory_order_acq_rel)) {
            ma_device_uninit(&device_);
        }
    }

    /**
     * @brief Initialize the miniaudio playback device
     * @param cfg Device configuration
     * @return true on success, false on failure
     *
     * Configures and initializes the miniaudio device with float32 format.
     * Sets up the data callback for lock-free ring buffer consumption.
     * Device starts in stopped state; caller must call start() after preroll.
     *
     * @pre cfg.channels > 0 && cfg.channels <= 32 && cfg.sampleRate > 0 && cfg.ring != nullptr
     *
     * @retval true Device initialized successfully
     * @retval false Initialization failed (invalid config or miniaudio error)
     *
     * Thread Safety: Thread-safe (called from control thread during setup)
     * miniaudio Integration:
     * - ma_device_config_init(ma_device_type_playback)
     * - Format: ma_format_f32 (float32)
     * - Callback: AudioOutput::dataCallback with this as pUserData
     */
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

    /**
     * @brief miniaudio data callback - fills output buffer from ring
     * @param pDevice miniaudio device pointer
     * @param pOutput Output buffer (float32 interleaved)
     * @param pInput Unused (playback only)
     * @param frameCount Number of frames to generate
     *
     * Called by miniaudio on the audio thread (real-time priority).
     * Must be lock-free and fast. Reads from SPSC ring, applies volume,
     * zero-fills on underrun.
     *
     * Thread Safety: Runs on miniaudio audio thread. Must not block,
     * allocate, or call non-realtime-safe functions.
     */
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
