/**
 * @file decoder_interface.cppm
 * @brief Abstract decoder interface for audio format decoders
 * @ingroup caudio_player
 *
 * This module defines the IDecoder abstract interface that all audio decoders must implement.
 * It mirrors the C ca_decoder_vt virtual table plus the sample_rate, channels, and total_frames
 * fields from the C decoder structure. All decoders (FFmpeg, etc.) inherit from this interface.
 *
 * Thread Safety: Implementations should be thread-compatible (safe for single-threaded use).
 * The Player class ensures decode() is called from a single dedicated decode thread.
 * seek() may be called from the control thread while decode() runs on the decode thread;
 * implementations must handle this synchronization internally.
 */

module;
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>

export module caudio.player:decoder_interface;

import caudio.utils;
import :reader;

export namespace caudio::player {

/**
 * @class IDecoder
 * @brief Abstract base class for audio format decoders
 * @ingroup caudio_player
 *
 * All audio decoders (FFmpeg, Vorbis, etc.) must implement this interface.
 * It provides the standard operations needed for audio playback: format info,
 * sample decoding, and seeking.
 *
 * Thread Safety: NOT thread-safe for concurrent calls. The Player ensures:
 * - decode() is called from a single dedicated decode thread
 * - seek() may be called from control thread; implementations must synchronize
 * - sampleRate(), channels(), totalFrames() are thread-safe (const noexcept)
 *
 * Error Codes (via Expected<void>):
 * - InvalidArg: Invalid seek time (negative, NaN, infinity)
 * - Io: Decoder-level I/O error during seek
 * - Internal: Decoder not initialized or internal error
 */
class IDecoder {
  public:
    virtual ~IDecoder() = default;

    /**
     * @brief Get the sample rate of the decoded audio
     * @return Sample rate in Hz (e.g., 44100, 48000, 96000)
     *
     * Returns the native sample rate of the audio stream. This value is set
     * during decoder initialization and does not change.
     *
     * Thread Safety: Thread-safe (const noexcept)
     */
    [[nodiscard]] virtual uint32_t sampleRate() const noexcept = 0;

    /**
     * @brief Get the number of audio channels
     * @return Channel count (1=mono, 2=stereo, etc.)
     *
     * Returns the number of channels in the decoded audio stream.
     * Output is always interleaved float samples.
     *
     * Thread Safety: Thread-safe (const noexcept)
     */
    [[nodiscard]] virtual uint32_t channels() const noexcept = 0;

    /**
     * @brief Get total frame count in the audio stream
     * @return Total frames (samples per channel), or 0 if unknown
     *
     * Returns the estimated total frame count for the entire stream.
     * Returns 0 for streams with unknown duration (e.g., live streams, some MP3).
     *
     * Thread Safety: Thread-safe (const noexcept)
     */
    [[nodiscard]] virtual uint64_t totalFrames() const noexcept = 0;

    /**
     * @brief Decode audio samples into the provided buffer
     * @param out Output buffer for interleaved float samples (size = frames * channels)
     * @return Number of frames decoded (0 = EOF or error)
     *
     * Decodes up to out.size() / channels() frames of audio into the buffer.
     * Returns the number of frames actually decoded. Returns 0 on EOF or error.
     * The buffer must be large enough for at least one frame (channels() samples).
     *
     * Thread Safety: Must be called from decode thread only (single-threaded).
     * Not safe for concurrent calls with seek().
     */
    virtual std::size_t decode(std::span<float> out) = 0;

    /**
     * @brief Seek to a specific time position in the stream
     * @param seconds Target time in seconds from stream start (must be >= 0, finite, not NaN)
     * @return void on success, Error on failure
     *
     * Seeks the decoder to the specified time position. The decoder will
     * resume decoding from this position on the next decode() call.
     *
     * @retval StatusCode::InvalidArg seconds is negative, NaN, or infinity
     * @retval StatusCode::Io Seek operation failed at decoder level
     * @retval StatusCode::Internal Decoder not properly initialized
     *
     * Thread Safety: May be called from control thread while decode() runs
     * on decode thread. Implementation must synchronize internally.
     */
    [[nodiscard]] virtual caudio::utils::Expected<void> seek(double seconds) = 0;
};

} // namespace caudio::player
