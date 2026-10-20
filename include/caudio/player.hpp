#pragma once

/**
 * @file player.hpp
 * @brief Umbrella header for the caudio.player module.
 * @ingroup caudio_player
 * @defgroup caudio_player caudio player
 *
 * Audio playback module providing:
 * - Media reader/decoder re-exports (see @ref caudio_media)
 * - Lock-free SPSC ring buffer for decode/audio thread communication
 * - miniaudio-based AudioOutput for device playback
 * - Player with gapless playback, seeking, and state management
 */

#include <caudio/media.hpp>
#include <caudio/player/decoder.hpp>
#include <caudio/player/output.hpp>
#include <caudio/player/player.hpp>
#include <caudio/player/reader.hpp>
