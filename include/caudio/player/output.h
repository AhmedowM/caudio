#ifndef CAUDIO_PLAYER_OUTPUT_H
#define CAUDIO_PLAYER_OUTPUT_H

/**
 * @file caudio/player/output.h
 * @brief Audio output device over miniaudio, consuming a lock-free SPSC ring.
 * @ingroup caudio_player
 *
 * AudioOutput manages a miniaudio playback device (float32 interleaved) that
 * consumes samples from a ca_ring filled by the decode thread. Designed for
 * real-time playback with no allocations in the audio callback.
 *
 * @par Thread safety
 * - create()/init()/start()/stop()/setVolume()/volume(): thread-safe, called
 *   from the control thread.
 * - The audio callback runs on miniaudio's audio thread (real-time) and reads
 *   from the ring lock-free; it never allocates.
 *
 * @par Lifecycle
 *   create() (calls init()) -> start() -> (playback) -> stop() -> shutdown()
 *   A zero-initialized object may call init() explicitly. shutdown() is
 *   idempotent and is called by destroy().
 *
 * miniaudio integration:
 * - ma_device_config_init(ma_device_type_playback)
 * - Format: ma_format_f32 (float32 interleaved)
 * - Data callback reads from ca_ring, applies volume, zero-fills underrun.
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>
#include <caudio/runtime/log.h>
#include <caudio/types/record.h>
#include <caudio/utils/ring.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio output construction parameters.
 */
typedef struct ca_audio_output_config {
    uint32_t sample_rate;  /**< Output sample rate in hertz (default 48000). */
    uint32_t channels;     /**< Output channel count (default 2, 1..32). */
    ca_ring* ring;         /**< Non-owning SPSC ring consumed by the callback. */
    float volume;          /**< Initial volume, clamped to [0, 1] (default 1.0). */
    ca_logger* logger;     /**< Non-owning logger; null disables logging. */
} ca_audio_output_config;

/**
 * @brief Initialize @p cfg to defaults (48000 Hz, 2 channels, volume 1.0).
 */
CAUDIO_API void ca_audio_output_config_init(ca_audio_output_config* cfg);

/**
 * @brief Opaque audio output handle.
 */
typedef struct ca_audio_output ca_audio_output;

/**
 * @brief Create and initialize an AudioOutput.
 *
 * @param[out] out Receives the new output handle on success.
 * @param cfg Configuration (sample rate, channels, ring, volume, logger).
 * @param err Optional error record.
 * @return CA_OK on success, CA_INVALID_ARGUMENT for invalid config,
 *         CA_DEVICE on miniaudio device init failure,
 *         CA_OUT_OF_MEMORY on allocation failure.
 *
 * @par Thread safety Thread-safe; may be called from any thread.
 */
[[nodiscard]] CAUDIO_API ca_status ca_audio_output_create(ca_audio_output** out,
                                              const ca_audio_output_config* cfg,
                                              ca_error* err);

/**
 * @brief Set playback volume (clamped to [0, 1], applied atomically).
 *
 * Takes effect on the next audio callback.
 *
 * @par Thread safety Thread-safe.
 */
CAUDIO_API void ca_audio_output_set_volume(ca_audio_output* self, float vol);

/**
 * @brief Return the current playback volume (0.0 to 1.0).
 *
 * @par Thread safety Thread-safe.
 */
[[nodiscard]] CAUDIO_API float ca_audio_output_volume(const ca_audio_output* self);

/**
 * @brief Start audio playback.
 *
 * Starts the miniaudio device if initialized; a compare_exchange ensures only
 * one start transition occurs. May block briefly while the backend starts.
 *
 * @par Thread safety Thread-safe.
 */
CAUDIO_API void ca_audio_output_start(ca_audio_output* self);

/**
 * @brief Stop audio playback.
 *
 * Stops the miniaudio device if running; a compare_exchange ensures only one
 * stop transition occurs.
 *
 * @par Thread safety Thread-safe.
 */
CAUDIO_API void ca_audio_output_stop(ca_audio_output* self);

/**
 * @brief Shut down and release audio device resources.
 *
 * Stops the device if running, then uninitializes the miniaudio device. Safe
 * to call multiple times. Called automatically by ca_audio_output_destroy.
 *
 * @par Thread safety Thread-safe; idempotent.
 */
CAUDIO_API void ca_audio_output_shutdown(ca_audio_output* self);

/**
 * @brief Initialize a (default-constructed or shut-down) output.
 *
 * @pre cfg.channels > 0 && cfg.channels <= 32 && cfg.sample_rate > 0 &&
 *      cfg.ring != nullptr
 *
 * @return true on success, false on failure (invalid config or miniaudio error).
 *
 * @par Thread safety Thread-safe during setup; do not reconfigure a running
 *                    device.
 */
[[nodiscard]] CAUDIO_API bool ca_audio_output_init(ca_audio_output* self,
                                         const ca_audio_output_config* cfg,
                                         ca_error* err);

/**
 * @brief Destroy an audio output, shutting it down first.
 *
 * Passing nullptr is a no-op.
 */
CAUDIO_API void ca_audio_output_destroy(ca_audio_output* self);

/* --------------------------------------------------------------- */
/* Audio device enumeration                                        */
/* --------------------------------------------------------------- */

/**
 * @brief Enumerate available audio playback devices.
 *
 * @param alloc Allocator for the returned list and its entries, or nullptr
 *              for the default allocator.
 * @param[out] out Receives the newly allocated device list on success.
 * @param err Optional error record.
 * @return CA_OK on success, CA_OUT_OF_MEMORY on allocation failure. On
 *         miniaudio context initialization failure, returns an empty list
 *         (CA_OK) rather than an error.
 *
 * @par Thread safety Thread-safe; each call creates and destroys its own
 *                    miniaudio context.
 */
[[nodiscard]] CAUDIO_API ca_status ca_audio_output_enumerate_devices(
    const cx_allocator* alloc, ca_device_list** out, ca_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_PLAYER_OUTPUT_H */
