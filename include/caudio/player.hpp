#pragma once

/**
 * @file player.hpp
 * @brief Umbrella header for the caudio.player module.
 * @ingroup caudio_player
 * @defgroup caudio_player caudio player
 *
 * Audio playback module providing:
 * - Reader abstractions (FileReader, MemoryReader) for input sources
 * - FFmpeg-based Decoder for all common formats
 * - Lock-free SPSC ring buffer for decode/audio thread communication
 * - miniaudio-based AudioOutput for device playback
 * - Player with gapless playback, seeking, and state management
 */

#include <caudio/player/core.hpp>
#include <caudio/player/decoder.hpp>
#include <caudio/player/output.hpp>
#include <caudio/player/reader.hpp>
// #include <caudio/utils.hpp>
