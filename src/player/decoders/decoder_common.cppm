module;
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

export module caudio.player:decoder_common;

namespace caudio::player::detail {

constexpr double kPi = 3.14159265358979323846;

inline std::size_t fillSine(std::span<float> out, std::size_t frames, uint32_t ch, uint32_t rate,
                            uint64_t& pos, uint64_t total, float amp, float chanOff) {
    if (out.empty() || frames == 0)
        return 0;
    uint64_t rem = pos < total ? total - pos : 0;
    if (rem == 0)
        return 0;
    std::size_t n = frames;
    if (static_cast<uint64_t>(n) > rem)
        n = static_cast<std::size_t>(rem);
    for (std::size_t i = 0; i < n; ++i) {
        uint64_t idx = pos + i;
        double t = static_cast<double>(idx) / static_cast<double>(rate);
        double s = std::sin(2.0 * kPi * 440.0 * t) * static_cast<double>(amp);
        for (uint32_t c = 0; c < ch; ++c) {
            out[i * ch + c] =
                static_cast<float>(s + static_cast<double>(c) * static_cast<double>(chanOff));
        }
    }
    pos += n;
    return n;
}

} // namespace caudio::player::detail