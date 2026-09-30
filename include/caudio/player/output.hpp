/**
 * @file output.hpp
 * @brief Audio output management using miniaudio
 * @ingroup caudio_player
 *
 * This header provides audio device enumeration and playback functionality
 * using the miniaudio library. It includes:
 * - DeviceInfo/DeviceList: Audio device metadata structures
 * - enumerateDevices(): System audio device enumeration via miniaudio context
 * - AudioOutput: Lock-free audio playback with ring buffer integration
 *
 * Thread Safety:
 * - enumerateDevices(): Thread-safe (creates/destroys miniaudio context internally)
 * - AudioOutput::create(): Thread-safe
 * - AudioOutput methods: Thread-safe for control (start/stop/setVolume),
 *   the audio callback runs on the audio thread (real-time priority)
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

#pragma once

#include <atomic>
#include <caudio/utils/error.hpp>
#include <caudio/utils/ring.hpp>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

/// Opaque miniaudio device handle; full type visible only in output.cpp.
struct ma_device;

namespace caudio::player {

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
DeviceList enumerateDevices();

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
 * - audio callback: runs on the audio thread (real-time), lock-free
 * - fillFromRing(): Thread-safe (no shared mutable state)
 *
 * Audio Pipeline:
 * 1. Decode thread writes float samples to SPSC ring buffer
 * 2. the backend invokes the audio callback on the audio thread
 * 3. the callback calls fillFromRing() to read from ring + apply volume
 * 4. Zero-fills any remaining buffer space (silence on underrun)
 *
 * Volume Control:
 * - Volume is applied in the audio callback via atomic load (lock-free)
 * - Range: 0.0 to 1.0, clamped with utils::clampVolume()
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

    /** @brief Default-constructs an uninitialized output; use create(). */
    AudioOutput() = default;

    /** @brief Shuts down the device. */
    ~AudioOutput();

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
    static Expected create(const Config& cfg);

    /**
     * @brief Set playback volume
     * @param vol Volume level (0.0 = mute, 1.0 = full)
     *
     * Volume is clamped to [0.0, 1.0] and applied atomically.
     * Takes effect on the next audio callback.
     *
     * Thread Safety: Thread-safe (atomic store with relaxed ordering)
     */
    void setVolume(float vol);

    /**
     * @brief Get current playback volume
     * @return Current volume level (0.0 to 1.0)
     *
     * Thread Safety: Thread-safe (atomic load with relaxed ordering)
     */
    float volume() const noexcept;

    /**
     * @brief Fill output buffer from ring buffer with volume scaling
     * @param out Output buffer to fill (interleaved float samples)
     * @param ring SPSC ring buffer to read from (may be null)
     * @param channels Number of channels in output
     * @param vol Volume multiplier (0.0 to 1.0)
     *
     * Lock-free helper that reads available samples from the ring buffer,
     * zero-fills any remainder, and applies volume scaling. No allocations.
     * Used by the audio callback.
     *
     * Thread Safety: Thread-safe when ring is the SPSC ring (single producer,
     * single consumer). No internal locking.
     */
    static void fillFromRing(std::span<float> out, caudio::utils::SpscRing<float>* ring,
                             uint32_t channels, float vol) noexcept;

    /**
     * @brief Check if audio output is currently playing
     * @return true if device is running, false otherwise
     *
     * Thread Safety: Thread-safe (atomic load with acquire ordering)
     */
    bool isPlaying() const noexcept;

    /**
     * @brief Start audio playback
     *
     * Starts the miniaudio device if initialized. Uses compare_exchange
     * to ensure only one start transition occurs.
     *
     * Thread Safety: Thread-safe (atomic compare_exchange)
     * Precondition: init() must have been called successfully
     */
    void start();

    /**
     * @brief Stop audio playback
     *
     * Stops the miniaudio device if running. Uses compare_exchange
     * to ensure only one stop transition occurs.
     *
     * Thread Safety: Thread-safe (atomic compare_exchange)
     */
    void stop();

    /**
     * @brief Shutdown and release audio device resources
     *
     * Stops the device if running, then uninitializes the miniaudio device.
     * Safe to call multiple times. Called automatically by destructor.
     *
     * Thread Safety: Thread-safe (atomic exchange for initialized flag)
     */
    void shutdown() noexcept;

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
     */
    bool init(const Config& cfg);

  private:
    /// Audio-thread entry point (backend trampoline); defined in output.cpp.
    static void dataCallback(struct ma_device* pDevice, void* pOutput, const void* pInput,
                             std::uint32_t frameCount);
    /// Opaque audio backend (miniaudio device); defined in output.cpp.
    struct DeviceState;
    Config cfg_;
    std::unique_ptr<DeviceState> device_;
    std::atomic<float> volume_{1.0f};
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
};

} // namespace caudio::player
