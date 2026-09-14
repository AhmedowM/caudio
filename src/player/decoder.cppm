/**
 * @file decoder.cppm
 * @brief Decoder registry and format probing for audio file decoding
 * @ingroup caudio_player
 *
 * This module provides the DecoderRegistry class which handles automatic format detection
 * and decoder instantiation. It implements a probe-based approach matching the C reference
 * implementation (ca_decode.c) to identify supported audio formats and create appropriate
 * decoder instances.
 *
 * Currently supports FFmpeg-based decoding for all common audio formats including:
 * OGG/Vorbis, FLAC, MP3, WAV, M4A/AAC, Opus, and WMA.
 */

module;
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

export module caudio.player:decoder;

import caudio.utils;
import :reader;
import :decoder_interface;
import :ffmpeg;

export namespace caudio::player {

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
    [[nodiscard]] static caudio::utils::Expected<std::unique_ptr<IDecoder>> open(Reader& reader) {
        constexpr std::size_t kProbeBytes = 32;
        std::array<std::byte, kProbeBytes> buf{};
        std::size_t n = 0;
        int64_t orig = reader.tell();
        if (orig < 0)
            orig = 0;

        // probe from start: seek to 0, read, then restore BEFORE create — ca_decode.c:60-72
        (void)reader.seek(0, SEEK_SET);
        n = reader.read(std::span<std::byte>(buf.data(), buf.size()));
        // restore to orig before probing/choosing — matches ca_decode.c
        {
            auto sr0 = reader.seek(orig, SEEK_SET);
            if (!sr0.has_value())
                (void)reader.seek(0, SEEK_SET);
        }

        std::span<const std::byte> probeSpan(buf.data(), n);

        // Try FFmpeg (handles all supported formats: OGG/FLAC/MP3/WAV/M4A/AAC/Opus/WMA)
        caudio::utils::Expected<std::unique_ptr<IDecoder>> result = std::unexpected(
            caudio::utils::Error{caudio::utils::StatusCode::Unsupported, "no decoder matched"});

        if (FfmpegDecoder::probe(probeSpan)) {
            // FFmpeg init expects file at 0 (start of container). C's ca_decode.c
            // restores to orig before open, but that is for decoders that can start
            // at arbitrary offset — FFmpeg's AVIO owns position after open and must
            // start at 0. Always seek to 0 before create; do NOT restore after success
            // or AVIO pos and file pos will diverge (causing Header missing/CRC).
            (void)reader.seek(0, SEEK_SET);
            result = FfmpegDecoder::create(reader);
            if (result.has_value()) {
                return result;
            }
            // failure: restore orig
            auto sr = reader.seek(orig, SEEK_SET);
            if (!sr.has_value())
                (void)reader.seek(0, SEEK_SET);
        }

        return result;
    }
};

} // namespace caudio::player
