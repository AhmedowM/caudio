#pragma once

/**
 * @file reader.hpp
 * @brief Compatibility aliases for reader abstractions moved to caudio.media.
 * @ingroup caudio_player
 * @details Compatibility alias header; prefer `caudio::media` directly.
 */

#include <caudio/media/reader.hpp>

namespace caudio::player {

/**
 * @brief Compatibility alias of `caudio::media::Reader`.
 * @ingroup caudio_player
 * @deprecated Prefer `caudio::media::Reader`.
 */
using Reader = caudio::media::Reader;

/**
 * @brief Compatibility alias of `caudio::media::FileReader`.
 * @ingroup caudio_player
 * @deprecated Prefer `caudio::media::FileReader`.
 */
using FileReader = caudio::media::FileReader;

/**
 * @brief Compatibility alias of `caudio::media::MemoryReader`.
 * @ingroup caudio_player
 * @deprecated Prefer `caudio::media::MemoryReader`.
 */
using MemoryReader = caudio::media::MemoryReader;

} // namespace caudio::player
