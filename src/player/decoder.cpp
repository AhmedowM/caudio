#include <array>
#include <caudio/player/decoder.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

#include "decoders/ffmpeg_impl.hpp"

namespace caudio::player {

bool Decoder::probe(std::span<const std::byte> data) noexcept {
    // When FFmpeg is available, be permissive - try to decode anything
    // FFmpeg's avformat_open_input will fail gracefully if format not supported
    // Only skip obviously empty data
    return data.size() >= 4;
}

caudio::utils::Expected<std::unique_ptr<Decoder>> Decoder::open(Reader& reader) {
    constexpr std::size_t kProbeBytes = 32;
    std::array<std::byte, kProbeBytes> buf{};
    std::size_t n = 0;
    int64_t orig = reader.tell();
    if (orig < 0)
        orig = 0;

    // probe from start: seek to 0, read, then restore BEFORE create -- ca_decode.c:60-72
    (void)reader.seek(0, SEEK_SET);
    n = reader.read(std::span<std::byte>(buf.data(), buf.size()));
    // restore to orig before probing/choosing -- matches ca_decode.c
    {
        auto sr0 = reader.seek(orig, SEEK_SET);
        if (!sr0.has_value())
            (void)reader.seek(0, SEEK_SET);
    }

    std::span<const std::byte> probeSpan(buf.data(), n);

    // Try FFmpeg (handles all supported formats: OGG/FLAC/MP3/WAV/M4A/AAC/Opus/WMA)
    caudio::utils::Expected<std::unique_ptr<Decoder>> result =
        std::unexpected(caudio::utils::Error{caudio::utils::StatusCode::Unsupported,
                                             std::string_view("no decoder matched")});

    if (Decoder::probe(probeSpan)) {
        // FFmpeg init expects file at 0 (start of container). C's ca_decode.c
        // restores to orig before open, but that is for decoders that can start
        // at arbitrary offset -- FFmpeg's AVIO owns position after open and must
        // start at 0. Always seek to 0 before create; do NOT restore after success
        // or AVIO pos and file pos will diverge (causing Header missing/CRC).
        (void)reader.seek(0, SEEK_SET);
        auto dec = std::make_unique<Decoder>();
        if (auto ok = dec->init(reader); ok.has_value()) {
            return caudio::utils::Expected<std::unique_ptr<Decoder>>{std::move(dec)};
        } else {
            // failure: restore orig
            auto sr = reader.seek(orig, SEEK_SET);
            if (!sr.has_value())
                (void)reader.seek(0, SEEK_SET);
            return std::unexpected{ok.error()};
        }
    }

    return result;
}

} // namespace caudio::player
