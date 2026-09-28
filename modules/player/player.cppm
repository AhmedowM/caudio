/**
 * @brief Re-export anchor for the caudio.player module (group defined in player.hpp).
 *
 * Audio playback module providing:
 * - Reader abstractions (FileReader, MemoryReader) for input sources
 * - FFmpeg-based Decoder for all common formats
 * - Lock-free SPSC ring buffer for decode/audio thread communication
 * - miniaudio-based AudioOutput for device playback
 * - Player core with gapless playback, seeking, and state management
 */
module;
#include <string_view>

export module caudio.player;

export import :reader;
export import :decoder;
export import :output;
export import :player_core;

import caudio.utils;
