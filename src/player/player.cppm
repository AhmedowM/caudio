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
module;
#include <string_view>

export module caudio.player;

export import :reader;
export import :decoder_common;
export import :decoder_interface;
export import :decoder;
export import :output;
export import :player_core;
export import :ffmpeg;

import caudio.utils;
