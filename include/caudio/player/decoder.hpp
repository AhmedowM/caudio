/**
 * @file decoder.hpp
 * @brief Decoder registry and format probing for audio file decoding
 * @ingroup caudio_player
 *
 * This header provides the DecoderRegistry class which handles automatic format detection
 * and decoder instantiation. It implements a probe-based approach matching the C reference
 * implementation (ca_decode.c) to identify supported audio formats and create appropriate
 * decoder instances.
 *
 * Currently supports FFmpeg-based decoding for all common audio formats including:
 * OGG/Vorbis, FLAC, MP3, WAV, M4A/AAC, Opus, and WMA.
 */

#pragma once

#include <memory>

#include <caudio/player/reader.hpp>
#include <caudio/utils.hpp>

namespace caudio::player {

class IDecoder;

/**
 * @class DecoderRegistry
 * @brief Central registry for audio format probing and decoder instantiation
 * @ingroup caudio_player
 *
 * The DecoderRegistry implements a probe-based decoder selection mechanism.
 * It reads a small header from the input (32 bytes), probes for supported formats,
 * and instantiates the appropriate decoder.
 *
 * Thread Safety: This class is stateless and thread-safe. All methods are static
 * and can be called concurrently from multiple threads.
 *
 * Probe Algorithm (matching ca_decode.c:48-72):
 * 1. Save current reader position
 * 2. Seek to start (offset 0) and read 32 bytes
 * 3. Restore original position BEFORE probing
 * 4. Probe FFmpeg decoder with the header data
 * 5. If probe succeeds, seek to 0 and create decoder (FFmpeg AVIO requires start at 0)
 * 6. On decoder creation failure, restore original position
 *
 * Error Codes:
 * - Unsupported: No decoder matched the probe data
 * - Io: FFmpeg initialization failed
 * - InvalidArg: Reader returned invalid position
 */
class DecoderRegistry {
  public:
    /**
     * @brief Probe and open a decoder for the given reader
     * @param reader Input reader positioned at start of audio data
     * @return Expected containing unique_ptr to IDecoder on success, Error on failure
     *
     * This function implements the complete probe-and-create workflow:
     * - Reads 32-byte header from reader start
     * - Tests FFmpeg decoder probe (handles all supported formats)
     * - On successful probe, creates decoder with reader at position 0
     * - Restores reader position on failure
     *
     * The reader's position is modified during operation but restored to its
     * original value on failure. On success, the reader position will be at
     * the start of the audio data (0) as required by FFmpeg's AVIO.
     *
     * @retval StatusCode::Unsupported No decoder recognized the format
     * @retval StatusCode::Io FFmpeg initialization failed
     * @retval StatusCode::InvalidArg Reader seek/tell returned invalid values
     */
    [[nodiscard]] static caudio::utils::Expected<std::unique_ptr<IDecoder>> open(Reader& reader);
};

} // namespace caudio::player
