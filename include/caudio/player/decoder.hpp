#pragma once

/**
 * @file decoder.hpp
 * @brief Compatibility aliases for the decoder moved to caudio.media.
 * @ingroup caudio_player
 * @details Compatibility alias header; prefer `caudio::media` directly.
 */

#include <caudio/media/decoder.hpp>

namespace caudio::player {

/**
 * @brief Compatibility alias of `caudio::media::Decoder`.
 * @ingroup caudio_player
 * @deprecated Prefer `caudio::media::Decoder`.
 */
using Decoder = caudio::media::Decoder;

/**
 * @brief Compatibility alias of `caudio::media::extractMetadata`.
 * @ingroup caudio_player
 * @deprecated Prefer `caudio::media::extractMetadata`.
 */
using caudio::media::extractMetadata;

/**
 * @brief Audio file metadata tags.
 * @ingroup caudio_player
 * @details Compatibility alias of `caudio::types::TrackMetadata`.
 * @deprecated Spell `caudio::types::TrackMetadata` directly.
 */
using TrackMetadata = caudio::types::TrackMetadata;

} // namespace caudio::player
