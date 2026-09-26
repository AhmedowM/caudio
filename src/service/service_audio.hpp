#pragma once

/**
 * @file service_audio.hpp
 * @brief Audio file probing for dispatch handlers (internal).
 * @ingroup caudio_service
 */

#include <filesystem>

namespace caudio::service::detail {

/**
 * @brief Get audio duration from decoder.
 * Opens file reader and decoder to read sample rate and total frames.
 * @param path Audio file path.
 * @return Duration in seconds, or 0.0 on failure.
 */
double durationFromDecoder(const std::filesystem::path& path) noexcept;

} // namespace caudio::service::detail
