#pragma once

#include <algorithm>
#include <cmath>

namespace caudio::utils {

/**
 * @brief Clamps a volume value to the valid range [0.0, 1.0].
 * @ingroup caudio_utils
 * @param v Volume value to clamp.
 * @return Clamped volume; 0.0 for NaN/infinity.
 */
[[nodiscard]] inline float clampVolume(float v) noexcept {
    if (!std::isfinite(v))
        return 0.0f;
    return std::clamp(v, 0.0f, 1.0f);
}

} // namespace caudio::utils
