module;
#include <string_view>

export module caudio.player;

export import :reader;
export import :decoder_common;
export import :decoder_interface;
export import :decoder;
export import :output;

#ifdef CAUDIO_WITH_FFMPEG
export import :ffmpeg;
#endif

import caudio.utils;

export namespace caudio::player {

constexpr std::string_view toString(caudio::utils::Result r) noexcept {
    return caudio::utils::toString(r);
}

} // namespace caudio::player
