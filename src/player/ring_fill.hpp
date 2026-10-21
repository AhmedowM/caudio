#pragma once

#include <algorithm>
#include <caudio/utils/ring.hpp>
#include <cstddef>
#include <cstdint>
#include <span>

namespace caudio::player::detail {

inline void fillFromRing(std::span<float> out, caudio::utils::SpscRing<float>* ring,
                         uint32_t channels, float volume) noexcept {
    if (out.empty())
        return;
    std::size_t totalSamples = out.size();
    std::size_t generatedFrames = 0;
    if (ring) {
        generatedFrames = ring->read(std::span<float>(out.data(), totalSamples));
    }
    std::size_t generatedSamples = generatedFrames * channels;
    if (generatedSamples < totalSamples) {
        std::ranges::fill(out.subspan(generatedSamples), 0.0f);
    }
    if (volume != 1.0f) {
        for (std::size_t i = 0; i < totalSamples; ++i)
            out[i] *= volume;
    }
}

} // namespace caudio::player::detail
