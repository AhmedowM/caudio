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

#ifdef CAUDIO_WITH_FFMPEG
import :ffmpeg;
#endif

export namespace caudio::player {

// DecoderRegistry — probe 32B then restore offset like ca_decode.c:48
// Priority: FFmpeg (all supported formats)
class DecoderRegistry {
  public:
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
            caudio::utils::Error{caudio::utils::Result::Unsupported, "no decoder matched"});

#ifdef CAUDIO_WITH_FFMPEG
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
#endif

        // restore offset on failure
        auto sr = reader.seek(orig, SEEK_SET);
        if (!sr.has_value()) {
            (void)reader.seek(0, SEEK_SET);
        }

        return result;
    }
};

} // namespace caudio::player