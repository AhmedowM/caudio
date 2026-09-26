/**
 * @defgroup caudio_player caudio player
 *
 * Audio playback module providing:
 * - Reader abstractions (FileReader, MemoryReader) for input sources
 * - Decoder registry with FFmpeg support for all common formats
 * - Lock-free SPSC ring buffer for decode/audio thread communication
 * - miniaudio-based AudioOutput for device playback
 * - Player core with gapless playback, seeking, and state management
 */

#pragma once

#include <caudio/player/decoder.hpp>
#include <caudio/player/output.hpp>
#include <caudio/player/player_core.hpp>
#include <caudio/player/reader.hpp>
#include <caudio/utils.hpp>
#include <string_view>
