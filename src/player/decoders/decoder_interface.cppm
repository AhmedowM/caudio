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

} // namespace caudio::player
