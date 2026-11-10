#ifndef CAUDIO_PLAYER_PLAYER_H
#define CAUDIO_PLAYER_PLAYER_H

/**
 * @file caudio/player/player.h
 * @brief Core audio player: decode thread, SPSC ring, miniaudio output.
 * @ingroup caudio_player
 *
 * The player coordinates an FFmpeg decoder (ca_decoder), a lock-free SPSC
 * ring buffer (ca_ring) and a miniaudio audio output (ca_audio_output), with
 * a dedicated decode thread that fills the ring while the audio device
 * consumes it.
 *
 * @par Architecture
 * - Control thread: application calls open/play/pause/stop/seek/set_volume.
 * - Decode thread: dedicated thread running the decode loop, fills the ring.
 * - Audio thread: miniaudio callback, consumes from the ring (real-time).
 *
 * @par Thread safety
 * All public methods are thread-safe; state transitions use atomics, and
 * open/stop are serialized by an internal mutex. The decoder/ring/output are
 * only touched by the decode thread (or under the open mutex during
 * open/stop/seek).
 *
 * @par Playback state machine
 *   Stopped <-> Playing <-> Paused
 *     ^         |          |
 *     |         v          v
 *     +-----> Error <------+
 *
 * @par Gapless playback
 * - open() prerolls the ring to half capacity before returning.
 * - seek() resets the ring, updates the position base atomically and
 *   restarts the decode thread when playing.
 * - Position is posBase (frames at last seek/start) plus monotonic elapsed
 *   time since playback started; it ignores device latency/underruns.
 *
 * @par Fixed player races (fixed before porting, not reproduced here)
 * - Decode/seek are serialized by the open mutex; the decode thread only
 *   runs when openGate and decodeBusy are both set.
 * - Reader/decoder/ring/output are replaced only after stop+join.
 * - The ring is reset only when the producer and consumer are quiesced.
 * - EOF is a distinct typed outcome (decode returns 0), not conflated with
 *   error or "buffer too small."
 */

#include <caudio/api.h>
#include <caudio/runtime/error.h>
#include <caudio/runtime/log.h>
#include <caudio/player/output.h>
#include <caudio/media/decoder.h>
#include <caudio/media/reader.h>
#include <caudio/utils/ring.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Playback state values.
 */
typedef enum ca_player_state {
    CA_PLAYER_STATE_STOPPED = 0,  /**< Not playing; opened or not opened. */
    CA_PLAYER_STATE_PLAYING = 1,  /**< Audio device running, decode active. */
    CA_PLAYER_STATE_PAUSED = 2,   /**< Audio device stopped, decode paused. */
    CA_PLAYER_STATE_ERROR = 3,    /**< A decoder/output failure occurred. */
} ca_player_state_t;

/**
 * @brief Player construction parameters.
 */
typedef struct ca_player_options {
    uint32_t sample_rate;  /**< Output sample rate in hertz (default 48000). */
    uint32_t channels;     /**< Output channel count (default 2). */
    ca_logger* logger;     /**< Non-owning logger; null disables logging. */
} ca_player_options;

/**
 * @brief Initialize @p opts to defaults (48000 Hz, 2 channels, no logger).
 */
CAUDIO_API void ca_player_options_init(ca_player_options* opts);

/**
 * @brief Opaque player handle.
 */
typedef struct ca_player ca_player;

/**
 * @brief Create a new player and start its decode thread.
 *
 * @param[out] out Receives the new player handle on success.
 * @param opts Configuration (sample rate, channels, logger).
 * @param err Optional error record.
 * @return CA_OK on success, CA_OUT_OF_MEMORY on allocation failure,
 *         CA_DEVICE if the decode thread could not be created.
 *
 * @par Thread safety Thread-safe; may be called from any thread.
 * The player starts in the Stopped state; call ca_player_open() before
 * ca_player_play().
 */
[[nodiscard]] CAUDIO_API ca_status ca_player_create(ca_player** out,
                                          const ca_player_options* opts,
                                          ca_error* err);

/**
 * @brief Destroy a player, stopping playback and joining the decode thread.
 *
 * Safe to call multiple times and safe from any thread.
 *
 * @par Thread safety Thread-safe.
 */
CAUDIO_API void ca_player_destroy(ca_player* self);

/**
 * @brief Open an audio file for playback.
 *
 * Creates a file reader from @p path, opens the decoder, ring buffer and
 * audio output, then prerolls the ring to half capacity.
 *
 * Supported formats (via FFmpeg): OGG/Vorbis, FLAC, MP3, WAV, M4A/AAC,
 * Opus, WMA.
 *
 * @param self Player handle.
 * @param path Filesystem path to the audio file (UTF-8).
 * @param err Optional error record.
 * @return CA_OK on success, CA_INVALID_ARGUMENT for an empty path,
 *         CA_NOT_FOUND if the file cannot be opened,
 *         CA_NOT_SUPPORTED if no decoder recognized the format,
 *         CA_IO_ERROR on FFmpeg initialization failure,
 *         CA_DEVICE if audio output creation failed,
 *         CA_OUT_OF_MEMORY on allocation failure.
 *
 * @par Thread safety Thread-safe; uses the internal open mutex for
 *                    exclusive access.
 * @par Ownership The player takes ownership of the reader and releases it
 *                when destroyed or reopened.
 */
[[nodiscard]] CAUDIO_API ca_status ca_player_open(ca_player* self,
                                        const char* path,
                                        ca_error* err);

/**
 * @brief Open playback from an existing reader (advanced usage).
 *
 * Stops any existing playback before opening the new reader. Used for
 * non-file sources such as memory buffers or descriptors.
 *
 * @param self Player handle.
 * @param reader Reader handle; ownership transfers to the player.
 * @param err Optional error record.
 * @return CA_OK on success, CA_INVALID_ARGUMENT for a null reader,
 *         CA_NOT_SUPPORTED / CA_IO_ERROR / CA_DEVICE / CA_OUT_OF_MEMORY
 *         as for ca_player_open.
 *
 * @par Thread safety Thread-safe; uses the internal open mutex.
 * @par Ownership The player takes ownership of @p reader.
 */
[[nodiscard]] CAUDIO_API ca_status ca_player_open_reader(ca_player* self,
                                             ca_reader* reader,
                                             ca_error* err);

/**
 * @brief Start or resume playback.
 *
 * Transitions Stopped -> Playing or Paused -> Playing. Starts the audio
 * device and signals the decode thread to begin filling the ring.
 *
 * @param self Player handle.
 * @param err Optional error record.
 * @return CA_OK on success, CA_STATE if not opened or already playing.
 *
 * @par Thread safety Thread-safe; uses an atomic state transition.
 */
[[nodiscard]] CAUDIO_API ca_status ca_player_play(ca_player* self, ca_error* err);

/**
 * @brief Pause playback.
 *
 * Transitions Playing -> Paused. Stops the audio device and updates position
 * tracking with elapsed time. The decode thread stops filling the ring.
 *
 * @param self Player handle.
 * @param err Optional error record.
 * @return CA_OK on success, CA_STATE if not currently playing.
 *
 * @par Thread safety Thread-safe; uses an atomic state transition.
 */
[[nodiscard]] CAUDIO_API ca_status ca_player_pause(ca_player* self, ca_error* err);

/**
 * @brief Resume playback from paused state.
 *
 * Transitions Paused -> Playing. Restarts the audio device and signals the
 * decode thread to continue. Position tracking resumes from the paused
 * position.
 *
 * @param self Player handle.
 * @param err Optional error record.
 * @return CA_OK on success, CA_STATE if not currently paused.
 *
 * @par Thread safety Thread-safe; uses an atomic state transition.
 */
[[nodiscard]] CAUDIO_API ca_status ca_player_resume(ca_player* self, ca_error* err);

/**
 * @brief Stop playback and reset to the Stopped state.
 *
 * Stops the audio device, resets the ring buffer and clears position
 * tracking. Does not close the decoder/reader; call ca_player_open() again
 * to play another track.
 *
 * @param self Player handle.
 * @param err Optional error record.
 * @return CA_OK on success.
 *
 * @par Thread safety Thread-safe; uses the internal open mutex.
 */
[[nodiscard]] CAUDIO_API ca_status ca_player_stop(ca_player* self, ca_error* err);

/**
 * @brief Seek to a time position in seconds from stream start.
 *
 * Seeks the decoder, resets the ring buffer and updates position tracking
 * atomically. If playing, restarts the decode thread from the new position;
 * if paused, updates position only.
 *
 * @param self Player handle.
 * @param seconds Target time in seconds (must be >= 0, finite).
 * @param err Optional error record.
 * @return CA_OK on success, CA_STATE if not opened, CA_INVALID_ARGUMENT for
 *         invalid seconds, CA_IO_ERROR on decoder seek failure.
 *
 * @par Thread safety Thread-safe; synchronizes decoder access and wakes the
 *                    decode thread.
 * @par Blocking May wait for decoder synchronization and device state changes.
 */
[[nodiscard]] CAUDIO_API ca_status ca_player_seek(ca_player* self,
                                        double seconds,
                                        ca_error* err);

/**
 * @brief Set playback volume.
 *
 * Volume is clamped to [0.0, 1.0] and applied atomically to the internal
 * state and the audio output device.
 *
 * @param self Player handle.
 * @param volume Volume level (0.0 = mute, 1.0 = full).
 * @param err Optional error record.
 * @return CA_OK on success, CA_INVALID_ARGUMENT if volume is NaN or infinite.
 *
 * @par Thread safety Thread-safe; stores the volume atomically.
 */
[[nodiscard]] CAUDIO_API ca_status ca_player_set_volume(ca_player* self,
                                              float volume,
                                              ca_error* err);

/**
 * @brief Return the current playback state.
 *
 * @param self Player handle.
 * @return Current state; CA_PLAYER_STATE_ERROR if a failure occurred.
 *
 * @par Thread safety Thread-safe; loads the state atomically.
 */
[[nodiscard]] CAUDIO_API ca_player_state_t ca_player_state(const ca_player* self);

/**
 * @brief Return the current playback position in seconds.
 *
 * Position is posBase (frames at last seek/start) plus steady-clock elapsed
 * time since playback started, divided by the output sample rate. Returns 0
 * when stopped or in error.
 *
 * @param self Player handle.
 * @return Position in seconds from stream start.
 *
 * @par Thread safety Thread-safe; reads position state atomically.
 */
[[nodiscard]] CAUDIO_API double ca_player_position(const ca_player* self);

/**
 * @brief Return the last error message, or an empty string if none.
 *
 * The returned pointer remains valid until the next state-changing call.
 *
 * @param self Player handle.
 * @return Last error message (never nullptr).
 */
[[nodiscard]] CAUDIO_API const char* ca_player_last_error(const ca_player* self);

#ifdef __cplusplus
}
#endif

#endif /* CAUDIO_PLAYER_PLAYER_H */
