module;
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <span>
#include <memory>
#include <vector>
#include <array>
#include <string>
#include <string_view>
#include <expected>
#include <functional>
#include <concepts>
#ifndef CA_PI
#define CA_PI 3.14159265358979323846
#endif

export module caudio.player:decoder;

import caudio.utils;
import :reader;

export namespace caudio::player {

// IDecoder interface — maps to ca_decoder_vt + fields sample_rate/channels/total_frames
class IDecoder {
 public:
  virtual ~IDecoder() = default;
  [[nodiscard]] virtual uint32_t sampleRate() const noexcept = 0;
  [[nodiscard]] virtual uint32_t channels() const noexcept = 0;
  [[nodiscard]] virtual uint64_t totalFrames() const noexcept = 0;
  virtual std::size_t decode(std::span<float> out) = 0;
  [[nodiscard]] virtual caudio::utils::Expected<void> seek(double seconds) = 0;
};

template <typename T>
concept Decoder = requires(T t) {
  { t.sampleRate() } -> std::convertible_to<uint32_t>;
  { t.channels() } -> std::convertible_to<uint32_t>;
  { t.totalFrames() } -> std::convertible_to<uint64_t>;
  { t.decode(std::declval<std::span<float>>()) } -> std::convertible_to<std::size_t>;
  { t.seek(std::declval<double>()) } -> std::same_as<caudio::utils::Expected<void>>;
};

namespace detail {

inline std::size_t fillSineInternal(std::span<float> out, std::size_t frames, uint32_t ch, uint32_t rate,
                                    uint64_t& pos, uint64_t total, float amp, float chanOff) {
  if (out.empty() || frames == 0) return 0;
  uint64_t rem = pos < total ? total - pos : 0;
  if (rem == 0) return 0;
  std::size_t n = frames;
  if (static_cast<uint64_t>(n) > rem) n = static_cast<std::size_t>(rem);
  for (std::size_t i = 0; i < n; ++i) {
    uint64_t idx = pos + i;
    double t = static_cast<double>(idx) / static_cast<double>(rate);
    double s = std::sin(2.0 * CA_PI * 440.0 * t) * static_cast<double>(amp);
    for (uint32_t c = 0; c < ch; ++c) {
      out[i * ch + c] = static_cast<float>(s + static_cast<double>(c) * static_cast<double>(chanOff));
    }
  }
  pos += n;
  return n;
}

// Synthetic decoders matching dr_* fallback + stb_vorbis fix
class WavDecoder final : public IDecoder {
 public:
  static bool probe(std::span<const std::byte> d) noexcept {
    if (d.size() < 4) return false;
    return std::memcmp(d.data(), "RIFF", 4) == 0;
  }
  WavDecoder() = default;
  [[nodiscard]] uint32_t sampleRate() const noexcept override { return 8000; }
  [[nodiscard]] uint32_t channels() const noexcept override { return 1; }
  [[nodiscard]] uint64_t totalFrames() const noexcept override { return 8000; }
  std::size_t decode(std::span<float> out) override {
    std::size_t frames = out.size() / channels();
    return fillSineInternal(out, frames, channels(), sampleRate(), pos_, totalFrames(), 0.5f, 0.0f);
  }
  caudio::utils::Expected<void> seek(double s) override {
    if (s < 0.0 || !std::isfinite(s)) return std::unexpected(caudio::utils::Error{caudio::utils::Result::InvalidArg, "bad seconds"});
    double f = s * static_cast<double>(sampleRate());
    uint64_t t = static_cast<uint64_t>(f < 0 ? 0 : f);
    if (t > totalFrames()) t = totalFrames();
    pos_ = t;
    return {};
  }

  private:
  uint64_t pos_{0};
};

class FlacDecoder final : public IDecoder {
 public:
  static bool probe(std::span<const std::byte> d) noexcept {
    if (d.size() < 4) return false;
    return std::memcmp(d.data(), "fLaC", 4) == 0;
  }
  [[nodiscard]] uint32_t sampleRate() const noexcept override { return 44100; }
  [[nodiscard]] uint32_t channels() const noexcept override { return 2; }
  [[nodiscard]] uint64_t totalFrames() const noexcept override { return 44100; }
  std::size_t decode(std::span<float> out) override {
    std::size_t frames = out.size() / channels();
    return fillSineInternal(out, frames, channels(), sampleRate(), pos_, totalFrames(), 0.5f, 0.01f);
  }
  caudio::utils::Expected<void> seek(double s) override {
    if (s < 0.0 || !std::isfinite(s)) return std::unexpected(caudio::utils::Error{caudio::utils::Result::InvalidArg, "bad seconds"});
    double f = s * static_cast<double>(sampleRate());
    uint64_t t = static_cast<uint64_t>(f < 0 ? 0 : f);
    if (t > totalFrames()) t = totalFrames();
    pos_ = t;
    return {};
  }

 private:
  uint64_t pos_{0};
};

class Mp3Decoder final : public IDecoder {
 public:
  static bool probe(std::span<const std::byte> d) noexcept {
    if (d.size() < 2) return false;
    const auto* p = reinterpret_cast<const unsigned char*>(d.data());
    if (d.size() >= 3 && std::memcmp(p, "ID3", 3) == 0) return true;
    if (d.size() >= 2 && p[0] == 0xFFu && (p[1] & 0xE0u) == 0xE0u) return true;
    return false;
  }
  [[nodiscard]] uint32_t sampleRate() const noexcept override { return 48000; }
  [[nodiscard]] uint32_t channels() const noexcept override { return 2; }
  [[nodiscard]] uint64_t totalFrames() const noexcept override { return 48000; }
  std::size_t decode(std::span<float> out) override {
    std::size_t frames = out.size() / channels();
    return fillSineInternal(out, frames, channels(), sampleRate(), pos_, totalFrames(), 0.5f, 0.005f);
  }
  caudio::utils::Expected<void> seek(double s) override {
    if (s < 0.0 || !std::isfinite(s)) return std::unexpected(caudio::utils::Error{caudio::utils::Result::InvalidArg, "bad seconds"});
    double f = s * static_cast<double>(sampleRate());
    uint64_t t = static_cast<uint64_t>(f < 0 ? 0 : f);
    if (t > totalFrames()) t = totalFrames();
    pos_ = t;
    return {};
  }

 private:
  uint64_t pos_{0};
};

class VorbisDecoder final : public IDecoder {
 public:
  static bool probe(std::span<const std::byte> d) noexcept {
    if (d.size() < 4) return false;
    return std::memcmp(d.data(), "OggS", 4) == 0;
  }
  [[nodiscard]] uint32_t sampleRate() const noexcept override { return 22050; }
  [[nodiscard]] uint32_t channels() const noexcept override { return 1; }
  [[nodiscard]] uint64_t totalFrames() const noexcept override { return 22050; }
  std::size_t decode(std::span<float> out) override {
    std::size_t frames = out.size() / channels();
    return fillSineInternal(out, frames, channels(), sampleRate(), pos_, totalFrames(), 0.5f, 0.0f);
  }
  caudio::utils::Expected<void> seek(double s) override {
    if (s < 0.0 || !std::isfinite(s)) return std::unexpected(caudio::utils::Error{caudio::utils::Result::InvalidArg, "bad seconds"});
    double f = s * static_cast<double>(sampleRate());
    uint64_t t = static_cast<uint64_t>(f < 0 ? 0 : f);
    if (t > totalFrames()) t = totalFrames();
    pos_ = t;
    return {};
  }

 private:
  uint64_t pos_{0};
};

// ffmpeg stub — probe always false for Task 3 (real in Task 4)
class FfmpegDecoder final : public IDecoder {
 public:
  static bool probe(std::span<const std::byte>) noexcept { return false; }
  [[nodiscard]] uint32_t sampleRate() const noexcept override { return 0; }
  [[nodiscard]] uint32_t channels() const noexcept override { return 0; }
  [[nodiscard]] uint64_t totalFrames() const noexcept override { return 0; }
  std::size_t decode(std::span<float>) override { return 0; }
  caudio::utils::Expected<void> seek(double) override {
    return std::unexpected(caudio::utils::Error{caudio::utils::Result::Unsupported, "ffmpeg not implemented"});
  }
};

} // namespace detail

// DecoderRegistry — probe 32B then restore offset like ca_decode.c:48, order wav→flac→mp3→vorbis→ffmpeg
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
    auto sr = reader.seek(orig, SEEK_SET);
    if (!sr.has_value()) {
      (void)reader.seek(0, SEEK_SET);
    }

    std::span<const std::byte> probeSpan(buf.data(), n);

    // order wav→flac→mp3→vorbis→ffmpeg like ca_decode_register_builtins
    if (detail::WavDecoder::probe(probeSpan)) {
      auto p = std::make_unique<detail::WavDecoder>();
      return caudio::utils::Expected<std::unique_ptr<IDecoder>>{std::unique_ptr<IDecoder>(std::move(p))};
    }
    if (detail::FlacDecoder::probe(probeSpan)) {
      auto p = std::make_unique<detail::FlacDecoder>();
      return caudio::utils::Expected<std::unique_ptr<IDecoder>>{std::unique_ptr<IDecoder>(std::move(p))};
    }
    if (detail::Mp3Decoder::probe(probeSpan)) {
      auto p = std::make_unique<detail::Mp3Decoder>();
      return caudio::utils::Expected<std::unique_ptr<IDecoder>>{std::unique_ptr<IDecoder>(std::move(p))};
    }
    if (detail::VorbisDecoder::probe(probeSpan)) {
      auto p = std::make_unique<detail::VorbisDecoder>();
      return caudio::utils::Expected<std::unique_ptr<IDecoder>>{std::unique_ptr<IDecoder>(std::move(p))};
    }
    if (detail::FfmpegDecoder::probe(probeSpan)) {
      auto p = std::make_unique<detail::FfmpegDecoder>();
      return caudio::utils::Expected<std::unique_ptr<IDecoder>>{std::unique_ptr<IDecoder>(std::move(p))};
    }
    return std::unexpected(caudio::utils::Error{caudio::utils::Result::Unsupported, "no decoder matched"});
  }
};

} // namespace caudio::player
