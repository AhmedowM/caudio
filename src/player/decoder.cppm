module;
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <span>
#include <memory>
#include <array>
#include <string>
#include <string_view>
#include <expected>
#include <functional>
#include <concepts>

export module caudio.player:decoder;

import caudio.utils;
import :reader;
import :decoder_interface;
import :wav;
import :flac;
import :mp3;
import :vorbis;

#ifdef CAUDIO_WITH_FFMPEG
import :ffmpeg;
#endif

export namespace caudio::player {

// DecoderRegistry — probe 32B then restore offset like ca_decode.c:48
// Priority: FFmpeg first (when available), then wav→flac→mp3→vorbis
class DecoderRegistry {
public:
  [[nodiscard]] static caudio::utils::Expected<std::unique_ptr<IDecoder>> open(Reader& reader) {
    constexpr std::size_t kProbeBytes = 32;
    std::array<std::byte, kProbeBytes> buf{};
    std::size_t n = 0;
    int64_t orig = reader.tell();
    if (orig < 0) orig = 0;

    // probe from start: seek to 0, read, then restore — ca_decode.c:60-72
    (void)reader.seek(0, SEEK_SET);
    n = reader.read(std::span<std::byte>(buf.data(), buf.size()));

    std::span<const std::byte> probeSpan(buf.data(), n);

    // Try FFmpeg first when available (ASF/WMA and other formats)
    caudio::utils::Expected<std::unique_ptr<IDecoder>> result =
        std::unexpected(caudio::utils::Error{caudio::utils::Result::Unsupported, "no decoder matched"});

#ifdef CAUDIO_WITH_FFMPEG
    if (FfmpegDecoder::probe(probeSpan)) {
      result = FfmpegDecoder::create(reader);
      if (result.has_value()) {
        // restore offset after create (which may read more) — ca_decode.c:60-72
        auto sr = reader.seek(orig, SEEK_SET);
        if (!sr.has_value()) {
          (void)reader.seek(0, SEEK_SET);
        }
        return result;
      }
    }
#endif

    // Fallback to dr_* decoders
    if (WavDecoder::probe(probeSpan)) {
      result = WavDecoder::create(reader);
    } else if (FlacDecoder::probe(probeSpan)) {
      result = FlacDecoder::create(reader);
    } else if (Mp3Decoder::probe(probeSpan)) {
      result = Mp3Decoder::create(reader);
    } else if (VorbisDecoder::probe(probeSpan)) {
      result = VorbisDecoder::create(reader);
    }

    // restore offset after create (which may read more) — ca_decode.c:60-72
    auto sr = reader.seek(orig, SEEK_SET);
    if (!sr.has_value()) {
      (void)reader.seek(0, SEEK_SET);
    }

    return result;
  }
};

} // namespace caudio::player