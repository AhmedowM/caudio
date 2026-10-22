#pragma once

/**
 * @file parse.hpp
 * @brief Parsers for the `caudio::types` wire enums.
 * @ingroup caudio_types
 * @details The inverse of `caudio/types/format.hpp`: turns the wire text
 * back into the enum. Only the exact spellings are accepted (case
 * sensitive); anything else yields `std::nullopt` so the caller can raise
 * its own error.
 *
 * Accepted spellings:
 * - `PlaybackState`: "Stopped", "Ready", "Playing", "Paused"
 * - `RepeatMode`: "Off", "All", "One"
 */

#include <caudio/types/wire.hpp>
#include <optional>
#include <string_view>

namespace caudio::types {

/**
 * @brief Parses the wire text of a PlaybackState.
 * @ingroup caudio_types
 * @param sv Text to parse ("Stopped", "Ready", "Playing", "Paused").
 * @return The parsed state, or `std::nullopt` for any other text.
 */
constexpr std::optional<PlaybackState> parsePlaybackState(std::string_view sv) noexcept {
    if (sv == "Stopped")
        return PlaybackState::Stopped;
    if (sv == "Ready")
        return PlaybackState::Ready;
    if (sv == "Playing")
        return PlaybackState::Playing;
    if (sv == "Paused")
        return PlaybackState::Paused;
    return std::nullopt;
}

/**
 * @brief Parses the wire text of a RepeatMode.
 * @ingroup caudio_types
 * @param sv Text to parse ("Off", "All", "One").
 * @return The parsed mode, or `std::nullopt` for any other text.
 */
constexpr std::optional<RepeatMode> parseRepeatMode(std::string_view sv) noexcept {
    if (sv == "Off")
        return RepeatMode::Off;
    if (sv == "All")
        return RepeatMode::All;
    if (sv == "One")
        return RepeatMode::One;
    return std::nullopt;
}

} // namespace caudio::types
