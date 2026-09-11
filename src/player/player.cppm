module;
#include <string_view>

export module caudio.player;

export import :reader;
export import :decoder_common;
export import :decoder_interface;
export import :decoder;
export import :output;
export import :player_core;

#ifdef CAUDIO_WITH_FFMPEG
export import :ffmpeg;
#endif

import caudio.utils;
